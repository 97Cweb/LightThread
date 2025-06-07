#include "LightThread.h"
#include "esp_mac.h"



void LightThread::handleUdpLine(const String& line) {
    log_d("UDP Received: %s", line.c_str());

    String srcIp = extractUdpSourceIp(line);
    if (srcIp.isEmpty()) {
        log_w("UDP message missing source IP.");
        return;
    }

    int hexStart = line.lastIndexOf(' ');
    if (hexStart == -1 || hexStart + 1 >= line.length()) {
        log_w("UDP message missing payload: %s", line.c_str());
        return;
    }

    String hexPayload = line.substring(hexStart + 1);
    hexPayload.trim();

    AckType ack;
    MessageType msg;
    std::vector<uint8_t> payload;

    if (!parseIncomingPayload(hexPayload, ack, msg, payload)) {
        log_w("Failed to parse UDP payload: %s", hexPayload.c_str());
        return;
    }

    log_d("Parsed UDP msg %02x ack %02x, payload %d bytes", static_cast<int>(msg), static_cast<int>(ack), static_cast<int>(payload.size()));

    if (ack == AckType::NONE && msg == MessageType::PAIRING && inState(State::JOINER_WAIT_BROADCAST)) {
        log_i("JOINER_WAIT_BROADCAST: Got PAIRING broadcast from %s", srcIp.c_str());

        // Respond with ID to leader directly
        std::vector<uint8_t> idBytes;
        uint64_t id = generateMacHash();
        for (int i = 7; i >= 0; --i)
            idBytes.push_back((id >> (i * 8)) & 0xFF);

        sendUdpPacket(AckType::REQUEST, MessageType::PAIRING, idBytes, srcIp, 12345);
        setState(State::JOINER_WAIT_ACK);
    }

    else if (ack == AckType::RESPONSE && msg == MessageType::PAIRING && inState(State::JOINER_WAIT_ACK)) {
		log_i("JOINER_WAIT_ACK: Got PAIRING RESPONSE from %s", srcIp.c_str());

		if (payload.size() != 8) {
			log_w("JOINER_WAIT_ACK: Expected 8-byte hashmac in response");
			setState(State::ERROR);
			return;
		}

		leaderIp = srcIp;

		uint64_t leaderHash = 0;
		for (int i = 0; i < 8; ++i) {
			leaderHash <<= 8;
			leaderHash |= payload[i];
		}

		String hashStr = String((uint32_t)(leaderHash >> 32), HEX) + String((uint32_t)(leaderHash & 0xFFFFFFFF), HEX);
		saveLeaderInfo(leaderIp, hashStr);

		setState(State::JOINER_PAIRED);
	}



    else if (ack == AckType::REQUEST && msg == MessageType::PAIRING && inState(State::COMMISSIONER_ACTIVE)) {
		uint64_t id = 0;
		for (size_t i = 0; i < payload.size() && i < 8; ++i) {
			id <<= 8;
			id |= payload[i];
		}

		String hashStr = String((uint32_t)(id >> 32), HEX) + String((uint32_t)(id & 0xFFFFFFFF), HEX);

		addJoinerEntry(srcIp, hashStr);
		log_i("COMMISSIONER_ACTIVE: Got joiner ID %016llx from %s — sending direct RESPONSE", id, srcIp.c_str());

		uint64_t selfHash = generateMacHash();
		std::vector<uint8_t> hashBytes;
		for (int i = 7; i >= 0; --i)
			hashBytes.push_back((selfHash >> (i * 8)) & 0xFF);

		sendUdpPacket(AckType::RESPONSE, MessageType::PAIRING, hashBytes, srcIp, 12345);

		log_i("COMMISSIONER_ACTIVE: Pairing complete, exiting commissioning");
		setState(State::STANDBY);
	}
	
	else if (ack == AckType::REQUEST && msg == MessageType::RECONNECT && role == Role::LEADER && inState(State::STANDBY)) {
		if (payload.size() != 8) {
			log_w("RECONNECT: Invalid payload from %s", srcIp.c_str());
			return;
		}

		uint64_t joinerId = 0;
		for (int i = 0; i < 8; ++i)
			joinerId = (joinerId << 8) | payload[i];

		String hashStr = String((uint32_t)(joinerId >> 32), HEX) + String((uint32_t)(joinerId & 0xFFFFFFFF), HEX);

		log_i("RECONNECT: Joiner %s [%s] is trying to find the leader", srcIp.c_str(), hashStr.c_str());

		uint64_t selfHash = generateMacHash();
		std::vector<uint8_t> hashBytes;
		for (int i = 7; i >= 0; --i)
			hashBytes.push_back((selfHash >> (i * 8)) & 0xFF);

		sendUdpPacket(AckType::RESPONSE, MessageType::RECONNECT, hashBytes, srcIp, 12345);
	}
	
	else if (ack == AckType::RESPONSE && msg == MessageType::RECONNECT && role == Role::JOINER) {
		if (payload.size() != 8) {
			log_w("RECONNECT: Invalid leader hash from %s", srcIp.c_str());
			return;
		}

		uint64_t receivedLeaderHash = 0;
		for (int i = 0; i < 8; ++i)
			receivedLeaderHash = (receivedLeaderHash << 8) | payload[i];

		uint64_t expectedHash = generateMacHash();  // Joiner's view of the leader hash (loaded at boot)
		String receivedStr = String((uint32_t)(receivedLeaderHash >> 32), HEX) + String((uint32_t)(receivedLeaderHash & 0xFFFFFFFF), HEX);

		String oldIp = leaderIp;
		leaderIp = srcIp;
		lastHeartbeatEcho = millis();

		log_i("RECONNECT: Leader responded from new IP %s [%s]", srcIp.c_str(), receivedStr.c_str());

		// Save new leader IP to disk
		saveLeaderInfo(leaderIp, receivedStr);
		if (joinCallback) {
			joinCallback(leaderIp, receivedStr);
			log_i("RECONNECT: Fired joinCallback with IP %s and hash %s", leaderIp.c_str(), receivedStr.c_str());
		}

		setState(State::JOINER_PAIRED);
	}


	
	else if (ack == AckType::NONE && msg == MessageType::HEARTBEAT && role == Role::LEADER) {
		if (payload.size() != 8) {
			log_w("HEARTBEAT: Invalid payload from %s", srcIp.c_str());
			return;
		}

		// Parse hashMAC from payload
		uint64_t id = 0;
		for (int i = 0; i < 8; ++i)
			id = (id << 8) | payload[i];

		String hashStr = String((uint32_t)(id >> 32), HEX) + String((uint32_t)(id & 0xFFFFFFFF), HEX);
		
		unsigned long now = millis();
		unsigned long lastSeen = joinerHeartbeatMap.count(srcIp) ? joinerHeartbeatMap[srcIp] : 0;
		joinerHeartbeatMap[srcIp] = now;

		log_i("HEARTBEAT: Joiner %s [%s] is alive", srcIp.c_str(), hashStr.c_str());

		// Echo heartbeat back
		sendUdpPacket(AckType::RESPONSE, MessageType::HEARTBEAT, payload, srcIp, 12345);

		// Trigger joinCallback if this is a reappearance
		const unsigned long silenceThreshold = 10000;
		if (lastSeen == 0 || now - lastSeen > silenceThreshold) {
			if (joinCallback) joinCallback(srcIp, hashStr);
			log_i("HEARTBEAT: Joiner %s [%s] reappeared — callback fired", srcIp.c_str(), hashStr.c_str());
		}
	}

	
	else if (ack == AckType::RESPONSE && msg == MessageType::HEARTBEAT && role == Role::JOINER) {
		lastHeartbeatEcho = millis();  // mark as acknowledged
		log_i("HEARTBEAT: Echo received from leader");
	}
	
	else if (msg == MessageType::NORMAL) {
		// Handle ACK first
		if (ack == AckType::RESPONSE && payload.size() >= 2) {
			uint16_t ackedId = (payload[0] << 8) | payload[1];
			if (pendingReliableMessages.erase(ackedId)) {
				if (reliableCallback) reliableCallback(ackedId, srcIp, true);
				log_i("ReliableUDP: ACK received for msgId %u", ackedId);
			} else {
				log_w("ReliableUDP: Unexpected ACK for msgId %u", ackedId);
			}
		}

		handleNormalUdpMessage(srcIp, payload, ack);

	}

}


String LightThread::extractUdpSourceIp(const String& line) {
    int fromIndex = line.indexOf("from ");
    if (fromIndex == -1) return "";

    int ipStart = fromIndex + 5;
    int ipEnd = line.indexOf(' ', ipStart);
    if (ipEnd == -1) return "";

    return line.substring(ipStart, ipEnd);
}




uint16_t LightThread::packMessage(AckType ack, MessageType type) {
    return (static_cast<uint16_t>(ack) << 8) | static_cast<uint8_t>(type);
}

void LightThread::unpackMessage(uint16_t raw, AckType& ack, MessageType& type) {
    ack = static_cast<AckType>((raw & 0xFF00) >> 8);
    type = static_cast<MessageType>(raw & 0x00FF);
}

bool LightThread::parseIncomingPayload(const String& hex, AckType& ack, MessageType& type, std::vector<uint8_t>& payloadOut) {
    std::vector<uint8_t> bytes;
    if (!convertHexToBytes(hex, bytes) || bytes.size() < 2) {
        log_w("Invalid or too short UDP payload: %s", hex.c_str());
        return false;
    }

    ack = static_cast<AckType>(bytes[0]);
    type = static_cast<MessageType>(bytes[1]);

    payloadOut.assign(bytes.begin() + 2, bytes.end());  // rest is data
    return true;
}



uint64_t LightThread::generateMacHash() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);  // Returns factory MAC

    uint64_t hash = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 6; ++i) {
        hash ^= mac[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool LightThread::sendUdpPacket(AckType ack, MessageType type, const std::vector<uint8_t>& payload, const String& destIp, uint16_t destPort,std::optional<uint16_t> messageId) {
    return sendUdpPacket(ack, type, payload.data(), payload.size(), destIp, destPort, messageId);
}

bool LightThread::sendUdpPacket(AckType ack, MessageType type, const uint8_t* payload, size_t length, const String& destIp, uint16_t destPort,std::optional<uint16_t> messageId) {
    if (destIp.isEmpty() || destPort == 0) {
        log_w("Invalid UDP destination");
        return false;
    }

    std::vector<uint8_t> fullMsg;
    fullMsg.push_back(static_cast<uint8_t>(ack));
    fullMsg.push_back(static_cast<uint8_t>(type));
	
	// Optional: messageId (2 bytes)
    if (messageId.has_value()) {
        fullMsg.push_back((messageId.value() >> 8) & 0xFF);
        fullMsg.push_back(messageId.value() & 0xFF);
    }
	
    fullMsg.insert(fullMsg.end(), payload, payload + length);

    String hex = convertBytesToHex(fullMsg.data(), fullMsg.size());

    String cmd = "udp send " + destIp + " " + String(destPort) + " " + hex;
    log_d("sendUdpPacket: %s", cmd.c_str());

    OThreadCLI.println(cmd);
    return true;
}

void LightThread::updateReliableUdp() {
    unsigned long now = millis();

    for (auto it = pendingReliableMessages.begin(); it != pendingReliableMessages.end(); ) {
        uint16_t msgId = it->first;
        PendingReliableUdp& msg = it->second;

        if (now - msg.timeSent >= 2000) {
            if (msg.retryCount >= 5) {
				log_w("ReliableUDP: Dropping msgId %u to %s", msgId, msg.destIp.c_str());
				if (reliableCallback) reliableCallback(msgId, msg.destIp, false);
				it = pendingReliableMessages.erase(it);
				continue;
			}


            log_i("ReliableUDP: Retrying msgId %u to %s (attempt %u)", msgId, msg.destIp.c_str(), msg.retryCount + 1);
            sendUdpPacket(AckType::REQUEST, MessageType::NORMAL, msg.payload, msg.destIp, 12345, msgId);
            msg.timeSent = now;
            msg.retryCount++;
        }

        ++it;
    }
}

void LightThread::attemptReconnectBroadcast() {
    uint64_t myHash = generateMacHash();
    std::vector<uint8_t> payload;
    for (int i = 7; i >= 0; --i)
        payload.push_back((myHash >> (i * 8)) & 0xFF);

    log_i("RECONNECT: Broadcasting query to find leader");
    sendUdpPacket(AckType::REQUEST, MessageType::RECONNECT, payload, "ff03::1", 12345);
}
