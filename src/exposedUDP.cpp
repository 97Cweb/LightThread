#include "LightThread.h"
#include <FS.h>
#include <SD.h>

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
	log_i("ExposedUDP: UDP callback registered");

}


void LightThread::registerJoinCallback(std::function<void(const String& ip, const String& hashmac)> cb) {
    joinCallback = cb;
    log_i("Join callback registered");
}

void LightThread::registerReliableUdpStatusCallback(std::function<void(uint16_t msgId, const String& ip, bool success)> cb) {
    reliableCallback = cb;
    log_i("Reliable UDP status callback registered");
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

std::map<String, String> LightThread::getKnownJoiners() {
    std::map<String, String> joiners;
    File file = SD.open("/cache/joiners.csv");
    if (!file) return joiners;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        int commaIndex = line.indexOf(',');
        if (commaIndex == -1) continue;

        String ip = line.substring(0, commaIndex);
        String hash = line.substring(commaIndex + 1);
        ip.trim(); hash.trim();
        if (!ip.isEmpty() && !hash.isEmpty()) {
            joiners[ip] = hash;
        }
    }

    file.close();
    return joiners;
}

unsigned long LightThread::getLastEchoTime(const String& ip) {
    if (joinerHeartbeatMap.count(ip)) {
        return joinerHeartbeatMap[ip];
    }
    return 0;  // Or millis() - large number to simulate "never"
}

bool LightThread::isReady() const {
    if (role == Role::LEADER) return state == State::STANDBY;
    if (role == Role::JOINER) return state == State::JOINER_PAIRED;
    return false;
}
