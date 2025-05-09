#include "LightThread.h"

bool LightThread::setDestination(const String& uniqueId) {
    for (int i = 0; i < joinerCount; i++) {
        if (joinerTable[i].uniqueId == uniqueId) {
            destinationIp = joinerTable[i].ip;
            destinationPort = joinerTable[i].port;
            return true;
        }
    }
    log_w("Unique ID not found in joiner table.");
    return false;
}


bool LightThread::sendData(const uint8_t* data, size_t length, AckType ackType) {
    if (!isState(JOINER_PAIRED) && !isState(COMMISSIONER_ACTIVE)) {
        log_w("Cannot send data: Invalid state.");
        return false;
    }

    String destIp;
    uint16_t destPort;

    if (role == JOINER) {
        // Joiners always send to the leader
        destIp = leaderIp;
        destPort = udpPort;
    } else {
        // Leader uses destination set via setDestination()
        destIp = destinationIp;
        destPort = destinationPort;
    }

    if (destIp.isEmpty() || destPort == 0) {
        log_w("No destination, cannot send UDP packet. AckType: %u  MessageType: %u", ackType, NORMAL);
        return false;
    }

    return sendUdpPacket(data, length, destIp, destPort, ackType, NORMAL);
}


const char* LightThread::getUniqueId() const {
    return hashMacAddress();
}

bool LightThread::isState(LightThreadState expectedState) const {
    return currentState == expectedState;
}