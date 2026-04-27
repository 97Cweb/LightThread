#include "LightThread.h"
#include "esp_mac.h"

// Parses a line from the CLI to see if it's a UDP message and attempts to parse it.
void LightThread::handleUdpLine(const String &line, const String &srcIp) {
    logLightThread(LT_LOG_INFO, "UDP Received: %s", line.c_str());

    if(srcIp.isEmpty()) {
        logLightThread(LT_LOG_WARN, "UDP message missing source IP.");
        return;
    }

    int hexStart = line.lastIndexOf(' ');
    if(hexStart == -1 || hexStart + 1 >= line.length()) {
        logLightThread(LT_LOG_WARN, "UDP message missing payload: %s", line.c_str());
        return;
    }

    String hexPayload = line.substring(hexStart + 1);
    hexPayload.trim();

    MessageType msg;
    std::vector<uint8_t> payload;

    if(!parseIncomingPayload(hexPayload, msg, payload)) {
        logLightThread(LT_LOG_WARN, "Failed to parse UDP payload: %s", hexPayload.c_str());
        return;
    }

    logLightThread(LT_LOG_INFO, "Parsed UDP msg %02x, payload %d bytes",
                   static_cast<int>(msg), static_cast<int>(payload.size()));

    if(msg == MessageType::PAIRING_BROADCAST &&
       inState(State::JOINER_WAIT_BROADCAST)) {
        logLightThread(LT_LOG_INFO, "JOINER_WAIT_BROADCAST: Got PAIRING broadcast from %s",
                       srcIp.c_str());

        // Respond with ID to leader directly
        std::vector<uint8_t> idBytes;
        uint64_t id = generateMacHash();
        for(int i = 7; i >= 0; --i)
            idBytes.push_back((id >> (i * 8)) & 0xFF);

        sendUdpPacket(MessageType::PAIRING_REQUEST, idBytes, srcIp, 12345);
        setState(State::JOINER_WAIT_ACK);
    }

    else if(msg == MessageType::PAIRING_RESPONSE &&
            inState(State::JOINER_WAIT_ACK)) {
        logLightThread(LT_LOG_INFO, "JOINER_WAIT_ACK: Got PAIRING RESPONSE from %s", srcIp.c_str());

        if(payload.size() != 8) {
            logLightThread(LT_LOG_ERROR, "JOINER_WAIT_ACK: Expected 8-byte hashmac in response");
            setState(State::ERROR);
            return;
        }

        leaderIp = srcIp;

        uint64_t leaderHash = 0;
        for(int i = 0; i < 8; ++i) {
            leaderHash <<= 8;
            leaderHash |= payload[i];
        }

        String hashStr = String((uint32_t)(leaderHash >> 32), HEX) +
                         String((uint32_t)(leaderHash & 0xFFFFFFFF), HEX);
        saveLeaderInfo(leaderIp, hashStr);

        setState(State::JOINER_PAIRED);
    }

    else if(msg == MessageType::PAIRING_REQUEST &&
            inState(State::COMMISSIONER_ACTIVE)) {
        uint64_t id = 0;
        for(size_t i = 0; i < payload.size() && i < 8; ++i) {
            id <<= 8;
            id |= payload[i];
        }

        String hashStr =
            String((uint32_t)(id >> 32), HEX) + String((uint32_t)(id & 0xFFFFFFFF), HEX);

        logLightThread(
            LT_LOG_INFO,
            "COMMISSIONER_ACTIVE: Got joiner ID %016llx from %s — sending direct RESPONSE", id,
            srcIp.c_str());

        uint64_t selfHash = generateMacHash();
        std::vector<uint8_t> hashBytes;
        for(int i = 7; i >= 0; --i) {
            hashBytes.push_back((selfHash >> (i * 8)) & 0xFF);
        }
        sendUdpPacket(MessageType::PAIRING_RESPONSE, hashBytes, srcIp, 12345);

        logLightThread(LT_LOG_INFO, "COMMISSIONER_ACTIVE: Pairing complete, exiting commissioning");
        setState(State::COMMISSIONER_STOPPING);
    }

    else if(msg == MessageType::RECONNECT_REQUEST && role == Role::LEADER &&
            inState(State::STANDBY)) {
        if(payload.size() != 8) {
            logLightThread(LT_LOG_WARN, "RECONNECT: Invalid payload from %s", srcIp.c_str());
            return;
        }

        uint64_t joinerId = 0;
        for(int i = 0; i < 8; ++i)
            joinerId = (joinerId << 8) | payload[i];

        String hashStr = String((uint32_t)(joinerId >> 32), HEX) +
                         String((uint32_t)(joinerId & 0xFFFFFFFF), HEX);

        logLightThread(LT_LOG_INFO, "RECONNECT: Joiner %s [%s] is trying to find the leader",
                       srcIp.c_str(), hashStr.c_str());

        uint64_t selfHash = generateMacHash();
        std::vector<uint8_t> hashBytes;
        for(int i = 7; i >= 0; --i)
            hashBytes.push_back((selfHash >> (i * 8)) & 0xFF);

        sendUdpPacket(MessageType::RECONNECT_RESPONSE, hashBytes, srcIp, 12345);
    }

    else if(msg == MessageType::RECONNECT_RESPONSE && role == Role::JOINER) {
        if(payload.size() != 8) {
            logLightThread(LT_LOG_WARN, "RECONNECT: Invalid leader hash from %s", srcIp.c_str());
            return;
        }

        uint64_t receivedLeaderHash = 0;
        for(int i = 0; i < 8; ++i)
            receivedLeaderHash = (receivedLeaderHash << 8) | payload[i];

        uint64_t expectedHash =
            generateMacHash(); // Joiner's view of the leader hash (loaded at boot)
        String receivedStr = String((uint32_t)(receivedLeaderHash >> 32), HEX) +
                             String((uint32_t)(receivedLeaderHash & 0xFFFFFFFF), HEX);

        String oldIp = leaderIp;
        leaderIp = srcIp;
        lastHeartbeatEcho = millis();

        logLightThread(LT_LOG_INFO, "RECONNECT: Leader responded from new IP %s [%s]",
                       srcIp.c_str(), receivedStr.c_str());

        // Save new leader IP to disk
        saveLeaderInfo(leaderIp, receivedStr);
        if(joinCallback) {
            joinCallback(leaderIp, receivedStr);
            logLightThread(LT_LOG_INFO, "RECONNECT: Fired joinCallback with IP %s and hash %s",
                           leaderIp.c_str(), receivedStr.c_str());
        }

        setState(State::JOINER_PAIRED);
    }

    else if(msg == MessageType::HEARTBEAT && role == Role::LEADER) {
        if(payload.size() != 8) {
            logLightThread(LT_LOG_WARN, "HEARTBEAT: Invalid payload from %s", srcIp.c_str());
            return;
        }

        // Parse hashMAC from payload
        uint64_t id = 0;
        for(int i = 0; i < 8; ++i)
            id = (id << 8) | payload[i];

        String hashStr =
            String((uint32_t)(id >> 32), HEX) + String((uint32_t)(id & 0xFFFFFFFF), HEX);

        unsigned long now = millis();
        unsigned long lastSeen;
        if(joinerHeartbeatMap.count(srcIp)) {
            lastSeen = joinerHeartbeatMap[srcIp];
        } else {
            lastSeen = 0;
        }
        joinerHeartbeatMap[srcIp] = now;

        logLightThread(LT_LOG_INFO, "HEARTBEAT: Joiner %s [%s] is alive", srcIp.c_str(),
                       hashStr.c_str());

        // Echo heartbeat back
        sendUdpPacket(MessageType::HEARTBEAT_ECHO, payload, srcIp, 12345);

        // Trigger joinCallback if this is a reappearance
        const unsigned long silenceThreshold = 10000;
        if(lastSeen == 0 || now - lastSeen > silenceThreshold) {
            if(joinCallback)
                joinCallback(srcIp, hashStr);
            logLightThread(LT_LOG_INFO, "HEARTBEAT: Joiner %s [%s] reappeared — callback fired",
                           srcIp.c_str(), hashStr.c_str());
        }
    }

    else if(msg == MessageType::HEARTBEAT_ECHO && role == Role::JOINER) {
        lastHeartbeatEcho = millis(); // mark as acknowledged
        logLightThread(LT_LOG_INFO, "HEARTBEAT: Echo received from leader");
    }

    else if(msg == MessageType::NORMAL) {
        handleNormalUdpMessage(srcIp, payload);
    }
}



bool LightThread::parseIncomingPayload(const String &hex, MessageType &type,
                                       std::vector<uint8_t> &payloadOut) {
    std::vector<uint8_t> bytes;
    if(!convertHexToBytes(hex, bytes) || bytes.size() < 1) {
        logLightThread(LT_LOG_WARN, "Invalid or too short UDP payload: %s", hex.c_str());
        return false;
    }

    type = static_cast<MessageType>(bytes[0]);

    payloadOut.assign(bytes.begin() + 1, bytes.end()); // rest is data
    return true;
}

uint64_t LightThread::generateMacHash() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac); // Returns factory MAC

    uint64_t hash = 0xcbf29ce484222325ULL;
    for(int i = 0; i < 6; ++i) {
        hash ^= mac[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Overload of sending a UDP UDP packet for a vector.
bool LightThread::sendUdpPacket(MessageType type, const std::vector<uint8_t> &payload,
                                const String &destIp, uint16_t destPort) {
    return sendUdpPacket(type, payload.data(), payload.size(), destIp, destPort);
}

// Sends a UDP packet with the given header and payload.
// Optionally enables reliable delivery (retry until ACK received).
bool LightThread::sendUdpPacket(MessageType type, const uint8_t *payload,
                                size_t length, const String &destIp, uint16_t destPort) {
    if(destIp.isEmpty() || destPort == 0) {
        logLightThread(LT_LOG_WARN, "Invalid UDP destination");
        return false;
    }

    std::vector<uint8_t> fullMsg;
    fullMsg.push_back(static_cast<uint8_t>(type));


    fullMsg.insert(fullMsg.end(), payload, payload + length);

    String hex = convertBytesToHex(fullMsg.data(), fullMsg.size());

    String cmd = "udp send " + destIp + " " + String(destPort) + " " + hex;
    logLightThread(LT_LOG_INFO, "sendUdpPacket: %s", cmd.c_str());

    OThreadCLI.println(cmd);
    return true;
}

void LightThread::captureMyIpFromResponse(const String &response) {
    int start = response.indexOf("fd");
    if(start == -1) start = response.indexOf("fe80");

    if(start == -1) {
        logLightThread(LT_LOG_WARN, "Could not parse MLEID from response: %s", response.c_str());
        return;
    }

    int end = response.indexOf('\n', start);
    if(end == -1) end = response.length();

    myIp = response.substring(start, end);
    myIp.trim();

    logLightThread(LT_LOG_INFO, "Captured my IP: %s", myIp.c_str());
}

