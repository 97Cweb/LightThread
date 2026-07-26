#include "LightThread.h"
#include "esp_mac.h"

void LightThread::receiveUdpPackets() {
    if(!udpOpen) {
        return;
    }

    int packetSize = 0;

    while((packetSize = udp.parsePacket()) > 0) {
        if(packetSize < static_cast<int>(
            LIGHTTHREAD_UDP_HEADER_BYTES
        )) {
            logLightThread(
                LIGHTTHREAD_LOG_WARN,
                "Received undersized UDP packet"
            );

            while(udp.available()) {
                udp.read();
            }

            continue;
        }

        IPAddress srcIp = udp.remoteIP();

        int typeByte = udp.read();

        if(typeByte < 0) {
            continue;
        }

        MessageType type =
            static_cast<MessageType>(
                static_cast<uint8_t>(typeByte)
            );

        std::vector<uint8_t> payload;
        payload.reserve(packetSize - 1);

        while(udp.available()) {
            int value = udp.read();

            if(value < 0) {
                break;
            }

            payload.push_back(
                static_cast<uint8_t>(value)
            );
        }

        handleUdpPacket(srcIp, type, payload);
    }
}

bool LightThread::sendUdpPacket(
    MessageType type,
    const std::vector<uint8_t> &payload,
    const IPAddress &destIp,
    uint16_t destPort
) {
    return sendUdpPacket(
        type,
        payload.data(),
        payload.size(),
        destIp,
        destPort
    );
}

bool LightThread::sendUdpPacket(
    MessageType type,
    const uint8_t *payload,
    size_t length,
    const IPAddress &destIp,
    uint16_t destPort
) {
  if(!udpOpen) {
      logLightThread(
          LIGHTTHREAD_LOG_WARN,
          "UDP socket is not open"
      );

      return false;
  }

  if(destPort == 0) {
    logLightThread(
        LIGHTTHREAD_LOG_WARN,
        "Invalid UDP destination port"
    );

    return false;
  }

  if(!udp.beginPacket(destIp, destPort)) {
    logLightThread(
      LIGHTTHREAD_LOG_WARN,"UDP beginPacket failed for %s",destIp.toString().c_str());

      return false;
  }

    uint8_t typeByte = static_cast<uint8_t>(type);

    if(udp.write(&typeByte, 1) != 1) {
        logLightThread(
            LIGHTTHREAD_LOG_WARN,
            "Failed to write UDP message type"
        );

        udp.endPacket();
        return false;
    }

    if(payload != nullptr && length > 0) {
        size_t written = udp.write(payload, length);

        if(written != length) {
            logLightThread(
                LIGHTTHREAD_LOG_WARN,
                "UDP payload truncated: %u/%u",
                static_cast<unsigned>(written),
                static_cast<unsigned>(length)
            );

            udp.endPacket();
            return false;
        }
    }

    if(!udp.endPacket()) {
        logLightThread(
            LIGHTTHREAD_LOG_WARN,
            "UDP endPacket failed"
        );

        return false;
    }

    return true;
}

void LightThread::handleUdpPacket(
    const IPAddress &srcIp,
    MessageType msg,
    const std::vector<uint8_t> &payload
) {
    String srcIpString = srcIp.toString();

    logLightThread(
        LIGHTTHREAD_LOG_INFO,
        "UDP type=%02x from %s, payload=%u",
        static_cast<unsigned>(msg),
        srcIpString.c_str(),
        static_cast<unsigned>(payload.size())
    );


    if(msg == MessageType::PAIRING_RESPONSE &&
       inState(State::JOINER_DISCOVER_LEADER)) {

        if(payload.size() != LIGHTTHREAD_HASH_BYTES) {
            logLightThread(
                LIGHTTHREAD_LOG_ERROR,
                "PAIRING_RESPONSE has invalid identity length"
            );

            return;
        }

        leaderIp = srcIp;

        if(joinCallback) {
            joinCallback(
                srcIpString,
                hashToString(bytesToHash(payload))
            );
        }

        setState(State::JOINER_PAIRED);
        return;
    }

    if(msg == MessageType::PAIRING_REQUEST &&
       inState(State::COMMISSIONER_ACTIVE)) {

        if(payload.size() != LIGHTTHREAD_HASH_BYTES) {
            logLightThread(
                LIGHTTHREAD_LOG_WARN,
                "PAIRING_REQUEST has invalid identity length"
            );
            return;
        }

        String joinerHash =
            hashToString(bytesToHash(payload));

        logLightThread(
            LIGHTTHREAD_LOG_INFO,
            "Joiner %s registered from %s",
            joinerHash.c_str(),
            srcIpString.c_str()
        );

        std::vector<uint8_t> leaderIdentity =
            hashToBytes(generateMacHash());

        if(!sendUdpPacket(
            MessageType::PAIRING_RESPONSE,
            leaderIdentity,
            srcIp,
            LIGHTTHREAD_UDP_PORT
        )){
          logLightThread(LIGHTTHREAD_LOG_WARN,"Could not send PAIRING_RESPONSE to %s", srcIpString.c_str());
          return;
        }

        if(joinCallback) {
            joinCallback(srcIpString, joinerHash);
        }

        setState(State::COMMISSIONER_STOPPING);
        return;
    }

    if(msg == MessageType::DISCOVERY_REQUEST &&
       role == Role::LEADER) {

        if(payload.size() != LIGHTTHREAD_HASH_BYTES) {
            logLightThread(
                LIGHTTHREAD_LOG_WARN,
                "DISCOVERY_REQUEST has invalid identity length"
            );

            return;
        }

        std::vector<uint8_t> leaderIdentity =
            hashToBytes(generateMacHash());

        sendUdpPacket(
            MessageType::DISCOVERY_RESPONSE,
            leaderIdentity,
            srcIp,
            LIGHTTHREAD_UDP_PORT
        );

        return;
    }

    if(msg == MessageType::DISCOVERY_RESPONSE &&
       role == Role::JOINER &&
       inState(State::JOINER_RECONNECT)) {

        if(payload.size() != LIGHTTHREAD_HASH_BYTES) {
            logLightThread(
                LIGHTTHREAD_LOG_WARN,
                "DISCOVERY_RESPONSE has invalid identity length"
            );

            return;
        }

        leaderIp = srcIp;

        logLightThread(
            LIGHTTHREAD_LOG_INFO,
            "Leader rediscovered at %s",
            srcIpString.c_str()
        );

        if(joinCallback) {
            joinCallback(
                srcIpString,
                hashToString(bytesToHash(payload))
            );
        }

        setState(State::JOINER_PAIRED);
        return;
    }

    if(msg == MessageType::NORMAL) {
        handleNormalUdpMessage(
            srcIpString,
            payload
        );
        return;
    }

    logLightThread(
        LIGHTTHREAD_LOG_WARN,
        "Unhandled UDP message type: %02x",
        static_cast<unsigned>(msg)
    );
}

uint64_t LightThread::generateMacHash() {
    uint8_t mac[LIGHTTHREAD_MAC_BYTES];

    esp_efuse_mac_get_default(mac);

    uint64_t hash =
        LIGHTTHREAD_FNV1A_OFFSET_BASIS;

    for(size_t i = 0;
        i < LIGHTTHREAD_MAC_BYTES;
        ++i) {

        hash ^= mac[i];
        hash *= LIGHTTHREAD_FNV1A_PRIME;
    }

    return hash;
}
