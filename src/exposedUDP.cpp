#include "LightThread.h"
#include <FS.h>
#include <SD.h>

// Handles incoming UDP message of type NORMAL.
// It calls the registered UDP callback.
void LightThread::handleNormalUdpMessage(const String &srcIp,
                                         const std::vector<uint8_t> &payload) {
    if (payload.empty()) return;
    if (udpCallback) {
        udpCallback(srcIp, payload);
    } else {
        logLightThread(LT_LOG_WARN, "ExposedUDP: No handler registered for NORMAL packets");
    }
}


// Registers a callback to receive parsed incoming UDP payloads (after stripping headers).
void LightThread::registerUdpReceiveCallback(
    std::function<void(const String &, const std::vector<uint8_t> &)> fn) {
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

// Sends a UDP packet to the destination IP.
bool LightThread::sendUdp(const String &destIp, const std::vector<uint8_t> &userPayload) {
    return sendUdpPacket(MessageType::NORMAL, userPayload, destIp, 12345);
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

