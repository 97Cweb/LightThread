#include "LightThread.h"

void LightThread::handleLeaderWaitNetwork() {
    static unsigned long lastCheck = 0;

    if (justEntered) {
		justEntered = false;
        log_i("LEADER_WAIT_NETWORK: Waiting for Thread network...");
        lastCheck = 0;  // Reset check timer
    }

    // Check every 5 seconds
    if (timeInState() - lastCheck < 5000) return;
    lastCheck = timeInState();

    String response;
    if (execAndMatch("state", "", &response, 1000)) {
        if (response.indexOf("leader") != -1 || response.indexOf("router") != -1) {
            log_i("LEADER_WAIT_NETWORK: Thread is up in state: %s", response.c_str());

            execAndMatch("udp open", "Done");
            execAndMatch("udp bind :: 12345", "Done");

            setState(State::STANDBY);
        } else {
            log_i("LEADER_WAIT_NETWORK: Not a leader yet");
        }
    } else {
        log_w("LEADER_WAIT_NETWORK: Failed to query state");
    }

    if (timeInState() > 50000) {
        log_e("LEADER_WAIT_NETWORK: Timed out waiting for leader state");
        setState(State::ERROR);
    }
}

void LightThread::handleCommissionerStart() {
    if (justEntered) {
		justEntered = false;
        execAndMatch("commissioner start", "Commissioner: active");
        execAndMatch("commissioner joiner add * J01NME", "Done");
    }

    if (timeInState() > 1000) {
        log_i("COMMISSIONER_START: Setup complete. Transitioning to COMMISSIONER_ACTIVE");
        setState(State::COMMISSIONER_ACTIVE);
    }
}
void LightThread::handleCommissionerActive() {
    static unsigned long lastBroadcast = 0;
    const unsigned long broadcastInterval = 3000; // 3 seconds

    if (millis() - lastBroadcast > broadcastInterval) {
        lastBroadcast = millis();

        std::vector<uint8_t> emptyPayload;
        bool ok = sendUdpPacket(
            AckType::NONE,
            MessageType::PAIRING,
            emptyPayload,
            "ff03::1",  // multicast all nodes
            12345
        );

        if (ok) {
            log_i("COMMISSIONER_ACTIVE: Sent PAIR_REQUEST broadcast");
        } else {
            log_w("COMMISSIONER_ACTIVE: Failed to send PAIR_REQUEST");
        }
    }
	
	if (timeInState() > 60000) {
        log_i("COMMISSIONER_ACTIVE: Pairing Timed out. Transitioning to STANDBY");
		execAndMatch("commissioner stop", "Done");
        setState(State::STANDBY);
    }
}
