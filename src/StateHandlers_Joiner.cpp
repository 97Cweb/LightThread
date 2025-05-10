#include "LightThread.h"

void LightThread::handleJoinerStart() {
    if (justEntered) {
		justEntered = false;
        log_i("JOINER_START: initializing joiner...");

        execAndMatch("dataset clear", "Done");
        execAndMatch("dataset panid 0xffff", "Done");
        execAndMatch("dataset channel 11", "Done");
        execAndMatch("dataset commit active", "Done");
        execAndMatch("ifconfig up", "Done");
        execAndMatch("udp open", "Done");
        execAndMatch("udp bind :: 12345", "Done");
        execAndMatch("joiner start J01NME", "Done");
    }

    if (timeInState() > 500) {
        execAndMatch("thread start", "Done");
        log_i("JOINER_START: Thread start issued");
        setState(State::JOINER_SCAN);
    }
}

void LightThread::handleJoinerScan() {
    static unsigned long lastCheck = 0;

    if (justEntered) {
		justEntered = false;
        log_i("JOINER_SCAN: checking joiner state...");
        lastCheck = 0;
    }

    if (timeInState() - lastCheck < 1000) return;
    lastCheck = timeInState();

    String response;
    if (execAndMatch("joiner state", "", &response, 2000)) {
        log_d("Joiner state response: %s", response.c_str());
        if (response.indexOf("Join failed") == -1 &&
            (response.indexOf("success") != -1 || response.indexOf("Idle") != -1)) {
            log_i("JOINER_SCAN: Joiner successfully paired");
            setState(State::JOINER_WAIT_BROADCAST);
        }
    } else {
        log_w("JOINER_SCAN: Failed to get joiner state");
    }
}

void LightThread::handleJoinerWaitBroadcast() {
    if (justEntered) {
		justEntered = false;
        log_i("JOINER_WAIT_BROADCAST: Listening for leader broadcast...");
    }

    if (!inState(State::JOINER_WAIT_BROADCAST)) return;
	
	if (timeInState() % 5000 < 50) {  // Roughly every 5s
		String stateResp;
		execAndMatch("state", "", &stateResp, 500);
		log_d("JOINER_WAIT_BROADCAST: current Thread state: %s", stateResp.c_str());
	}


    if (millis() - stateEntryTime > 20000) {
        log_w("JOINER_WAIT_BROADCAST: Timed out waiting for broadcast.");
        setState(State::STANDBY);
        return;
    }

    // No longer handles UDP explicitly here — handled globally in update()
}

void LightThread::handleJoinerWaitAck() {
    if (justEntered) {
		justEntered = false;
        log_i("JOINER_WAIT_ACK: Waiting for PAIR_ACK...");
    }

    if (timeInState() > 10000) { // 10s timeout
        log_w("JOINER_WAIT_ACK: Timed out waiting for ACK");
        setState(State::STANDBY);
    }
}
void LightThread::handleJoinerPaired() {
	if (justEntered) {
		justEntered = false;
        log_i("JOINER_PAIRED: storing configuration and entering standby");
		// In a real implementation: save leader IP, PAN ID, channel, etc.

    }
}
void LightThread::handleJoinerReconnect() {
    static bool configured = false;
    static unsigned long lastCheck = 0;

    if (justEntered) {
		justEntered = false;
        log_i("JOINER_RECONNECT: attempting reconnection to mesh...");
        configured = false;
        lastCheck = 0;
    }

    if (!configured) {
        execAndMatch("dataset commit active", "Done");
        execAndMatch("ifconfig up", "Done");
        execAndMatch("thread start", "Done");
        execAndMatch("udp open", "Done");
        execAndMatch("udp bind :: 12345", "Done");
        configured = true;
    }

    // Poll or heartbeat check every 5s if desired
    if (timeInState() - lastCheck >= 5000) {
        lastCheck = timeInState();
        log_d("JOINER_RECONNECT: Waiting for confirmation/heartbeat...");
        // Optionally check something like a ping or heartbeat here
    }

    // State transition is handled elsewhere based on incoming messages
}