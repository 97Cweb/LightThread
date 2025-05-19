#include "LightThread.h"

// Internal: stored callback from Beeton
static std::function<void(const String& srcIp, const std::vector<uint8_t>& payload)> udpCallback = nullptr;

// Called by UDPComm.cpp after MessageType::NORMAL is parsed
void LightThread::handleNormalUdpMessage(const String& srcIp, const std::vector<uint8_t>& payload) {
    log_d("ExposedUDP: Received NORMAL packet from %s (%d bytes)", srcIp.c_str(), payload.size());

    if (udpCallback) {
        udpCallback(srcIp, payload);
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
bool LightThread::sendExposedUdp(const String& destIp, bool reliable, const std::vector<uint8_t>& payload) {
    AckType ack = reliable ? AckType::REQUEST : AckType::NONE;
    return sendUdpPacket(ack, MessageType::NORMAL, payload, destIp, 12345);
}
