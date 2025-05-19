#include "LightThread.h"

// Internal: stored callback from Beeton
static std::function<void(const String& srcIp, const std::vector<uint8_t>& payload)> udpCallback = nullptr;

// Called by UDPComm.cpp after MessageType::NORMAL is parsed
void LightThread::handleNormalUdpMessage(const String& srcIp, const std::vector<uint8_t>& payload, AckType ack) {
    if (payload.empty()) return;

    std::vector<uint8_t> strippedPayload = payload;

    if (ack == AckType::REQUEST) {
        if (payload.size() < 2) {
            log_w("ExposedUDP: Reliable message too short for messageId");
            return;
        }

        uint16_t messageId = (payload[0] << 8) | payload[1];
        strippedPayload.erase(strippedPayload.begin(), strippedPayload.begin() + 2);

        std::vector<uint8_t> ackPayload = {
            static_cast<uint8_t>((messageId >> 8) & 0xFF),
            static_cast<uint8_t>(messageId & 0xFF)
        };

        sendUdpPacket(AckType::RESPONSE, MessageType::NORMAL, ackPayload, srcIp, 12345);
        log_i("ExposedUDP: Sent ACK for messageId %u to %s", messageId, srcIp.c_str());
    }

    if (udpCallback) {
        udpCallback(srcIp, strippedPayload);
    } else {
        log_w("ExposedUDP: No handler registered for NORMAL packets");
    }
}


// Public method: Called by Beeton to register its handler
void LightThread::registerUdpReceiveCallback(std::function<void(const String&, const std::vector<uint8_t>&)> fn) {
    udpCallback = fn;
    log_i("ExposedUDP: Beeton callback registered");
}

// Public method: Called by Beeton to send a raw payload to an IP
bool LightThread::sendUdp(const String& destIp, bool reliable, const std::vector<uint8_t>& userPayload) {
    if (!reliable) {
        return sendUdpPacket(AckType::NONE, MessageType::NORMAL, userPayload, destIp, 12345);
    }

    uint16_t msgId = nextMessageId++;
    pendingReliableMessages[msgId] = {
        .destIp = destIp,
        .payload = userPayload,
        .timeSent = millis(),
        .retryCount = 0
    };

    return sendUdpPacket(AckType::REQUEST, MessageType::NORMAL, userPayload, destIp, 12345,msgId);
}

