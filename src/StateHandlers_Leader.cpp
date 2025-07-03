#include "LightThread.h"

// Waits for the Thread network to come up and become a leader or router.
// Once stable, binds the UDP socket and transitions to STANDBY.
void LightThread::handleLeaderWaitNetwork() {
    static unsigned long lastCheck = 0;

    if(justEntered) {
        justEntered = false;
        logLightThread(LT_LOG_INFO, "LEADER_WAIT_NETWORK: Waiting for Thread network...");
        lastCheck = 0; // Reset check timer
    }

    // Check every 5 seconds
    if(timeInState() - lastCheck < 5000)
        return;
    lastCheck = timeInState();

    String response;
    if(execAndMatch("state", "", &response, 1000)) {
        if(response.indexOf("leader") != -1 || response.indexOf("router") != -1) {
            logLightThread(LT_LOG_INFO, "LEADER_WAIT_NETWORK: Thread is up in state: %s",
                           response.c_str());

            // Open UDP communication and bind to port 12345
            execAndMatch("udp open", "Done");
            execAndMatch("udp bind :: 12345", "Done");

            setState(State::STANDBY);
        } else {
            logLightThread(LT_LOG_INFO, "LEADER_WAIT_NETWORK: Not a leader yet");
        }
    } else {
        logLightThread(LT_LOG_WARN, "LEADER_WAIT_NETWORK: Failed to query state");
    }

    // Timeout if leader state isn't achieved in 50 seconds
    if(timeInState() > 50000) {
        logLightThread(LT_LOG_ERROR, "LEADER_WAIT_NETWORK: Timed out waiting for leader state");
        setState(State::ERROR);
    }
}

// Begins the commissioner role and adds a wildcard joiner filter.
void LightThread::handleCommissionerStart() {
    if(justEntered) {
        justEntered = false;
        // Start the commissioner
        execAndMatch("commissioner start", "Commissioner: active");
        // Add wildcard joiner (everyone can join)
        execAndMatch("commissioner joiner add * J01NME", "Done");
    }

    // Short delay before moving to broadcast phase
    if(timeInState() > 1000) {
        logLightThread(LT_LOG_INFO,
                       "COMMISSIONER_START: Setup complete. Transitioning to COMMISSIONER_ACTIVE");
        setState(State::COMMISSIONER_ACTIVE);
    }
}

// Sends pairing broadcasts periodically while in commissioner active mode.
// Transitions to STANDBY after 60 seconds.
void LightThread::handleCommissionerActive() {
    static unsigned long lastBroadcast = 0;
    const unsigned long broadcastInterval = 3000; // 3 seconds

    // Broadcast PAIRING signal
    if(millis() - lastBroadcast > broadcastInterval) {
        lastBroadcast = millis();

        std::vector<uint8_t> emptyPayload;
        bool ok = sendUdpPacket(AckType::NONE, MessageType::PAIRING, emptyPayload,
                                "ff03::1", // multicast all nodes
                                12345);

        if(ok) {
            logLightThread(LT_LOG_INFO, "COMMISSIONER_ACTIVE: Sent PAIR_REQUEST broadcast");
        } else {
            logLightThread(LT_LOG_WARN, "COMMISSIONER_ACTIVE: Failed to send PAIR_REQUEST");
        }
    }

    // End commissioning after 60 seconds
    if(timeInState() > 60000) {
        logLightThread(LT_LOG_INFO,
                       "COMMISSIONER_ACTIVE: Pairing Timed out. Transitioning to STANDBY");
        execAndMatch("commissioner stop", "Done");
        setState(State::STANDBY);
    }
}
