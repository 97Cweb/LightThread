#include "LightThread.h"

void LightThread::handleNormalUdpMessage(
    const String &srcIp,
    const std::vector<uint8_t> &payload
) {
    if(payload.empty()) {
        return;
    }

    if(!udpCallback) {
        logLightThread(
            LIGHTTHREAD_LOG_WARN,
            "No NORMAL UDP callback registered"
        );

        return;
    }

    udpCallback(srcIp, payload);
}

void LightThread::registerUdpReceiveCallback(
    std::function<void(
        const String &,
        const std::vector<uint8_t> &
    )> fn
) {
    udpCallback = fn;
}

void LightThread::registerJoinCallback(
    std::function<void(
        const String &,
        const String &
    )> cb
) {
    joinCallback = cb;
}

bool LightThread::sendUdp(
    const String &destIp,
    const std::vector<uint8_t> &payload
) {
    IPAddress address;

    if(!address.fromString(destIp)) {
        logLightThread(
            LIGHTTHREAD_LOG_WARN,
            "Invalid destination IP: %s",
            destIp.c_str()
        );

        return false;
    }

    return sendUdpPacket(
        MessageType::NORMAL,
        payload,
        address,
        LIGHTTHREAD_UDP_PORT
    );
}


bool LightThread::isReady() const {
    if(role == Role::LEADER) {
        return state == State::STANDBY;
    }

    return state == State::JOINER_PAIRED;
}
