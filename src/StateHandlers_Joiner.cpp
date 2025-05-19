#include "LightThread.h"

void LightThread::handleJoinerStart() {
    if (justEntered) {
		justEntered = false;
        log_i("JOINER_START: initializing joiner...");

        setupJoinerDataset();
		setupJoinerThreadDefaults();
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
    static bool escalated = false;
    static unsigned long lastCheck = 0;

    if (justEntered) {
        justEntered = false;
        escalated = false;
        lastCheck = millis();  // time marker
        log_i("JOINER_PAIRED: storing configuration and entering standby");
    }

    sendHeartbeatIfDue();

    if (!escalated && millis() - lastCheck > 5000) {
        String stateResp;
        if (execAndMatch("state", "", &stateResp, 1000)) {
            stateResp.toLowerCase();

            if (stateResp.indexOf("child") != -1) {
                String modeResp;
                if (execAndMatch("mode", "", &modeResp, 500)) {
                    modeResp.toLowerCase();
                    if (modeResp.indexOf("d") == -1) {
                        // Only switch if we're not already in 'd'
                        execAndMatch("mode rdn", "Done");
                        log_i("JOINER_PAIRED: Escalated to rdn (Thread state: child)");
                    } else {
                        log_d("JOINER_PAIRED: Already in rdn mode");
                    }
                }
                escalated = true;
            } else {
                log_d("JOINER_PAIRED: Still waiting for child state: %s", stateResp.c_str());
            }
        }
    }
}


void LightThread::handleJoinerReconnect() {
    static bool stackStarted = false;
    static unsigned long lastCheck = 0;

    if (justEntered) {
        justEntered = false;
        log_i("JOINER_RECONNECT: bringing up stack for auto-heal");
		
		setupJoinerDataset();
		setupJoinerThreadDefaults();
		execAndMatch("thread start", "Done");


        stackStarted = true;
        lastHeartbeatSent = 0;
        lastHeartbeatEcho = 0;
        lastCheck = 0;
    }

    sendHeartbeatIfDue();

    if (millis() - lastCheck > 2000) {
        lastCheck = millis();
        String resp;
        if (execAndMatch("state", "", &resp, 1000)) {
            resp.toLowerCase();
            if (resp.indexOf("child") != -1 || resp.indexOf("router") != -1) {
                log_i("JOINER_RECONNECT: back in mesh as %s", resp.c_str());
                setState(State::JOINER_PAIRED);
                return;
            }
        }
    }

    if (timeInState() > 120000) {
        log_w("JOINER_RECONNECT: Timeout — going to standby");
        setState(State::STANDBY);
    }
}

void LightThread::setupJoinerDataset() {
    execAndMatch("dataset clear", "Done");
	execAndMatch("dataset init new", "Done");  // REQUIRED
	execAndMatch("dataset panid " + configuredPanid, "Done");
	execAndMatch("dataset channel " + String(configuredChannel), "Done");
	execAndMatch("dataset meshlocalprefix " + configuredPrefix, "Done");
	execAndMatch("dataset networkkey 00112233445566778899aabbccddeeff", "Done");
	execAndMatch("dataset networkname OpenThreadMesh", "Done");  // REQUIRED

}

void LightThread::setupJoinerThreadDefaults() {
	String resp;
    execAndMatch("mode rn", "Done");               // Full router-capable node
    execAndMatch("routerselectionjitter 0", "Done"); // Never auto-promote to leader
	execAndMatch("routerupgradethreshold 255", "Done");
	execAndMatch("routerdowngradethreshold 1", "Done");  // So it never tries to stick as router if it ever gets one
    execAndMatch("dataset commit active", "Done");
	execAndMatch("dataset active", "", &resp, 1000);
	log_d("DATASET: %s", resp.c_str());

    execAndMatch("ifconfig up", "Done");
	execAndMatch("udp close", "Done");
    execAndMatch("udp open", "Done");
    execAndMatch("udp bind :: 12345", "Done");
}



void LightThread::sendHeartbeatIfDue() {
    if (leaderIp.isEmpty()) return;

    // Send every 5 seconds
    if (millis() - lastHeartbeatSent < 5000) return;

    // Timeout if no echo in 15 seconds
    if (lastHeartbeatEcho > 0 && millis() - lastHeartbeatEcho > 15000) {
        log_w("HEARTBEAT: Leader did not respond. Timing out.");
        setState(State::JOINER_RECONNECT);  // or STANDBY if reconnect logic not desired
        return;
    }

    lastHeartbeatSent = millis();

    uint64_t id = generateMacHash();
    std::vector<uint8_t> payload;
    for (int i = 7; i >= 0; --i)
        payload.push_back((id >> (i * 8)) & 0xFF);

    bool ok = sendUdpPacket(AckType::NONE, MessageType::HEARTBEAT, payload, leaderIp, 12345);
    if (ok) {
		log_i("HEARTBEAT: Sent to leader");
	} else {
		log_w("HEARTBEAT: Failed to send");
	}

}
