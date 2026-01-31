#include "LightThread.h"
#include <FS.h>
#include <SD.h>

// Handles incoming UDP message of type NORMAL.
// If the message is marked as reliable (AckType::REQUEST), an ACK is sent.
// Otherwise, it calls the registered UDP callback.
void LightThread::handleNormalUdpMessage(const String &srcIp,
                                         const std::vector<uint8_t> &payload,
                                         AckType ack) {
    if (payload.empty()) return;

    if(ack == AckType::RESPONSE) return;
    bool reliable = (ack == AckType::REQUEST);

    // Non-reliable: payload already IS the app payload (e.g., Beeton packet)
    // Reliable: payload begins with 2-byte messageId followed by app payload
    const std::vector<uint8_t>* forwarded = &payload;
    std::vector<uint8_t> strippedPayload;

    if (reliable) {
        if (payload.size() < 2) {
            logLightThread(LT_LOG_WARN, "ExposedUDP: Reliable message too short for messageId");
            return;
        }

        // Here messageId is at payload[0..1] because ack/type were already stripped
        uint16_t messageId = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];

        std::vector<uint8_t> ackPayload = {
            static_cast<uint8_t>((messageId >> 8) & 0xFF),
            static_cast<uint8_t>(messageId & 0xFF)
        };

        sendUdpPacket(AckType::RESPONSE, MessageType::NORMAL,
                      ackPayload, srcIp, 12345);

        logLightThread(LT_LOG_INFO, "ExposedUDP: Sent ACK for messageId %u to %s",
                       messageId, srcIp.c_str());

        // Strip messageId before forwarding to app
        strippedPayload.assign(payload.begin() + 2, payload.end());
        forwarded = &strippedPayload;
    }


    if (udpCallback) {
        udpCallback(srcIp, reliable, *forwarded);
    } else {
        logLightThread(LT_LOG_WARN, "ExposedUDP: No handler registered for NORMAL packets");
    }
}


// Registers a callback to receive parsed incoming UDP payloads (after stripping headers).
void LightThread::registerUdpReceiveCallback(
    std::function<void(const String &, bool reliable, const std::vector<uint8_t> &)> fn) {
    udpCallback = fn;
    logLightThread(LT_LOG_INFO, "ExposedUDP: UDP callback registered");
}

// Registers a callback that is triggered when a new joiner is detected.
// Used in pairing flows.
void LightThread::registerJoinCallback(
    std::function<void(const String &ip, const String &hashmac)> cb) {
    joinCallback = cb;
    logLightThread(LT_LOG_INFO, "Join callback registered");
}

// Registers a callback that is invoked upon delivery success/failure
// of a reliable UDP message.
void LightThread::registerReliableUdpStatusCallback(
    std::function<void(uint16_t msgId, const String &ip, bool success)> cb) {
    reliableCallback = cb;
    logLightThread(LT_LOG_INFO, "Reliable UDP status callback registered");
}

// Sends a UDP packet to the destination IP.
// If reliable is true, adds it to the retry queue and assigns a messageId.
bool LightThread::sendUdp(const String &destIp, bool reliable,
                          const std::vector<uint8_t> &userPayload) {
    if(!reliable) {
        return sendUdpPacket(AckType::NONE, MessageType::NORMAL, userPayload, destIp, 12345);
    }

    // Generate a new message ID
    uint16_t msgId = nextMessageId++;

    // Track this reliable message for retry and acknowledgment
    pendingReliableMessages[msgId] = {
        .destIp = destIp, .payload = userPayload, .timeSent = millis(), .retryCount = 0};

    // Send with ACK request
    return sendUdpPacket(AckType::REQUEST, MessageType::NORMAL, userPayload, destIp, 12345, msgId);
}

// Returns the last time (in millis) a heartbeat was received from the given IP.
// Used to detect lost joiners.
unsigned long LightThread::getLastEchoTime(const String &ip) {
    if(joinerHeartbeatMap.count(ip)) {
        return joinerHeartbeatMap[ip];
    }
    return 0; // never heard from, return 0
}

// Returns true if the system is in a ready state (based on role and state).
//   - Leader must be in STANDBY
//   - Joiner must be fully PAIRED
bool LightThread::isReady() const {
    if(role == Role::LEADER)
        return state == State::STANDBY;
    if(role == Role::JOINER)
        return state == State::JOINER_PAIRED;
    return false;
}

String LightThread::getMyIp() {
    String response;
    if (execAndMatch("ipaddr mleid", "Done", &response)) {
        // The CLI output will look like:
        // "fd00:db8:abcd::1234\nDone"
        // Let's strip the trailing "Done" and whitespace
        int end = response.indexOf("Done");
        if (end > 0) response = response.substring(0, end);
        response.trim();
        return response;
    } else {
        return "";
    }
}

