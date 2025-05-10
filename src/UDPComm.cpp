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
        setState(State::JOINER_PAIRED);
    }

    else if (ack == AckType::REQUEST && msg == MessageType::PAIRING && inState(State::COMMISSIONER_ACTIVE)) {
        uint64_t id = 0;
        for (size_t i = 0; i < payload.size() && i < 8; ++i) {
            id <<= 8;
            id |= payload[i];
        }

        log_i("COMMISSIONER_ACTIVE: Got joiner ID %016llx from %s — sending direct RESPONSE", id, srcIp.c_str());

        std::vector<uint8_t> empty;
        sendUdpPacket(AckType::RESPONSE, MessageType::PAIRING, empty, srcIp, 12345);
		log_i("COMMISSIONER_ACTIVE: Pairing complete, exiting commissioning");
		setState(State::STANDBY);

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

bool LightThread::sendUdpPacket(AckType ack, MessageType type, const std::vector<uint8_t>& payload, const String& destIp, uint16_t destPort) {
    return sendUdpPacket(ack, type, payload.data(), payload.size(), destIp, destPort);
}

bool LightThread::sendUdpPacket(AckType ack, MessageType type, const uint8_t* payload, size_t length, const String& destIp, uint16_t destPort) {
    if (destIp.isEmpty() || destPort == 0) {
        log_w("Invalid UDP destination");
        return false;
    }

    std::vector<uint8_t> fullMsg;
    fullMsg.push_back(static_cast<uint8_t>(ack));
    fullMsg.push_back(static_cast<uint8_t>(type));
    fullMsg.insert(fullMsg.end(), payload, payload + length);

    String hex = convertBytesToHex(fullMsg.data(), fullMsg.size());

    String cmd = "udp send " + destIp + " " + String(destPort) + " " + hex;
    log_d("sendUdpPacket: %s", cmd.c_str());

    OThreadCLI.println(cmd);
    return true;
}

