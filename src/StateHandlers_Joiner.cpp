#include "LightThread.h"

// Starts the joiner process by configuring dataset and launching join
void LightThread::handleJoinerStart() {
    if(justEntered) {
        justEntered = false;
        logLightThread(LT_LOG_INFO, "JOINER_START: initializing joiner...");

        setupJoinerDataset();                        // Sets network parameters
        setupJoinerThreadDefaults();                 // Configures thread options
        execAndMatch("joiner start J01NME", "Done"); // Start joiner role
    }

    // After a brief delay, start Thread stack
    if(timeInState() > 500) {
        execAndMatch("thread start", "Done");
        logLightThread(LT_LOG_INFO, "JOINER_START: Thread start issued");
        setState(State::JOINER_SCAN);
    }
}

// Checks for joiner success/failure and transitions accordingly
void LightThread::handleJoinerScan() {
    static unsigned long lastCheck = 0;

    if(justEntered) {
        justEntered = false;
        logLightThread(LT_LOG_INFO, "JOINER_SCAN: checking joiner state...");
        lastCheck = 0;
    }

    if(timeInState() - lastCheck < 1000)
        return;
    lastCheck = timeInState();

    String response;
    if(execAndMatch("joiner state", "", &response, 2000)) {
        logLightThread(LT_LOG_INFO, "Joiner state response: %s", response.c_str());
        if(response.indexOf("Join failed") == -1 &&
           (response.indexOf("success") != -1 || response.indexOf("Idle") != -1)) {
            logLightThread(LT_LOG_INFO, "JOINER_SCAN: Joiner successfully paired");
            setState(State::JOINER_WAIT_BROADCAST);
        }
    } else {
        logLightThread(LT_LOG_WARN, "JOINER_SCAN: Failed to get joiner state");
    }
}

// Waits for leader’s WHOAMI broadcast
void LightThread::handleJoinerWaitBroadcast() {
    if(justEntered) {
        justEntered = false;
        logLightThread(LT_LOG_INFO, "JOINER_WAIT_BROADCAST: Listening for leader broadcast...");
    }

    if(!inState(State::JOINER_WAIT_BROADCAST))
        return;

    // Log current state every ~5 seconds
    if(timeInState() % 5000 < 50) {
        String stateResp;
        execAndMatch("state", "", &stateResp, 500);
        logLightThread(LT_LOG_INFO, "JOINER_WAIT_BROADCAST: current Thread state: %s",
                       stateResp.c_str());
    }

    // Timeout fallback
    if(millis() - stateEntryTime > 20000) {
        logLightThread(LT_LOG_WARN, "JOINER_WAIT_BROADCAST: Timed out waiting for broadcast.");
        setState(State::STANDBY);
        return;
    }
}

// Waits for leader to acknowledge our response
void LightThread::handleJoinerWaitAck() {
    if(justEntered) {
        justEntered = false;
        logLightThread(LT_LOG_INFO, "JOINER_WAIT_ACK: Waiting for PAIR_ACK...");
    }

    if(timeInState() > 10000) { // 10s timeout
        logLightThread(LT_LOG_WARN, "JOINER_WAIT_ACK: Timed out waiting for ACK");
        setState(State::STANDBY);
    }
}

// Fully paired state — sends heartbeat, escalates if needed
void LightThread::handleJoinerPaired() {
    static bool escalated = false;
    static unsigned long lastCheck = 0;

    if(justEntered) {
        justEntered = false;
        escalated = false;
        lastCheck = millis(); // time marker
        logLightThread(LT_LOG_INFO, "JOINER_PAIRED: storing configuration and entering standby");
        if(joinCallback) {
            uint64_t myHash = generateMacHash();
            String hashStr = String((uint32_t)(myHash >> 32), HEX) +
                             String((uint32_t)(myHash & 0xFFFFFFFF), HEX);
            joinCallback(leaderIp, hashStr);
            logLightThread(LT_LOG_INFO, "JOINER_PAIRED: Fired joinCallback with IP %s and hash %s",
                           leaderIp.c_str(), hashStr.c_str());
        }
    }

    sendHeartbeatIfDue();

    // Optional escalation to router-delegation-node (rdn)
    if(!escalated && millis() - lastCheck > 5000) {
        String stateResp;
        if(execAndMatch("state", "", &stateResp, 1000)) {
            stateResp.toLowerCase();

            if(stateResp.indexOf("child") != -1) {
                String modeResp;
                if(execAndMatch("mode", "", &modeResp, 500)) {
                    modeResp.toLowerCase();
                    if(modeResp.indexOf("d") == -1) {
                        // Only switch if we're not already in 'd'
                        execAndMatch("mode rdn", "Done");
                        logLightThread(LT_LOG_INFO,
                                       "JOINER_PAIRED: Escalated to rdn (Thread state: child)");
                    } else {
                        logLightThread(LT_LOG_INFO, "JOINER_PAIRED: Already in rdn mode");
                    }
                }
                escalated = true;
            } else {
                logLightThread(LT_LOG_INFO, "JOINER_PAIRED: Still waiting for child state: %s",
                               stateResp.c_str());
            }
        }
    }
}

// Attempt to reconnect to last known leader
void LightThread::handleJoinerReconnect() {
    static bool stackStarted = false;
    static unsigned long lastCheck = 0;
    static bool reconnectBroadcastSent = false;

    if(justEntered) {
        justEntered = false;
        logLightThread(LT_LOG_INFO, "JOINER_RECONNECT: bringing up stack for auto-heal");

        setupJoinerDataset();
        setupJoinerThreadDefaults();
        execAndMatch("thread start", "Done");

        stackStarted = true;
        lastHeartbeatSent = 0;
        lastHeartbeatEcho = 0;
        lastCheck = 0;
        reconnectBroadcastSent = false;
    }

    sendHeartbeatIfDue();

    // Check if we're reattached to the mesh
    if(millis() - lastCheck > 2000) {
        lastCheck = millis();
        String resp;
        if(execAndMatch("state", "", &resp, 1000)) {
            resp.toLowerCase();
            if(resp.indexOf("child") != -1 || resp.indexOf("router") != -1) {
                logLightThread(LT_LOG_INFO, "JOINER_RECONNECT: back in mesh as %s", resp.c_str());
                setState(State::JOINER_PAIRED);
                return;
            }
        }
    }

    // Timeout and fallback
    if(timeInState() > 120000) {
        logLightThread(LT_LOG_WARN, "JOINER_RECONNECT: Timeout — going to standby");
        setState(State::STANDBY);
    }
}

// Called when actively retrying multicast reconnect
void LightThread::handleJoinerSeekingLeader() { sendHeartbeatIfDue(); }

// Heartbeat logic for JOINER: sends echo, triggers reconnect on timeout
void LightThread::sendHeartbeatIfDue() {
    if(leaderIp.isEmpty())
        return;

    // Send every 5 seconds
    if(millis() - lastHeartbeatSent < 5000)
        return;

    // No echo in 15s → assume leader is dead and trigger reconnect
    if(millis() - lastHeartbeatEcho > 15000) {
        logLightThread(LT_LOG_WARN, "HEARTBEAT: Leader not responding. Broadcasting reconnect.");

        // Send RECONNECT request over multicast with own hashMAC
        uint64_t myHash = generateMacHash();
        std::vector<uint8_t> payload;
        for(int i = 7; i >= 0; --i)
            payload.push_back((myHash >> (i * 8)) & 0xFF);

        sendUdpPacket(AckType::REQUEST, MessageType::RECONNECT, payload, "ff03::1", 12345);
        lastHeartbeatSent = millis(); // Rate-limit retries
        setState(State::JOINER_SEEKING_LEADER);
        return;
    }

    lastHeartbeatSent = millis();

    // Normal heartbeat to known leader IP
    uint64_t id = generateMacHash();
    std::vector<uint8_t> payload;
    for(int i = 7; i >= 0; --i)
        payload.push_back((id >> (i * 8)) & 0xFF);

    bool ok = sendUdpPacket(AckType::NONE, MessageType::HEARTBEAT, payload, leaderIp, 12345);
    if(ok) {
        logLightThread(LT_LOG_INFO, "HEARTBEAT: Sent to leader");
    } else {
        logLightThread(LT_LOG_WARN, "HEARTBEAT: Failed to send");
    }
}

// Prepares default dataset for joiner
void LightThread::setupJoinerDataset() {
    execAndMatch("dataset clear", "Done");
    execAndMatch("dataset init new", "Done"); // REQUIRED
    execAndMatch("dataset panid " + configuredPanid, "Done");
    execAndMatch("dataset channel " + String(configuredChannel), "Done");
    execAndMatch("dataset meshlocalprefix " + configuredPrefix, "Done");
    execAndMatch("dataset networkkey 00112233445566778899aabbccddeeff", "Done");
    execAndMatch("dataset networkname OpenThreadMesh", "Done"); // REQUIRED
}

// Applies default network and routing settings for joiners
void LightThread::setupJoinerThreadDefaults() {
    String resp;
    execAndMatch("mode rn", "Done");                 // Full router-capable node
    execAndMatch("routerselectionjitter 0", "Done"); // Never auto-promote to leader
    execAndMatch("routerupgradethreshold 255", "Done");
    execAndMatch("routerdowngradethreshold 1",
                 "Done"); // So it never tries to stick as router if it ever gets one
    execAndMatch("dataset commit active", "Done");
    execAndMatch("dataset active", "", &resp, 1000);
    logLightThread(LT_LOG_INFO, "DATASET: %s", resp.c_str());

    execAndMatch("ifconfig up", "Done");
    execAndMatch("udp close", "Done");
    execAndMatch("udp open", "Done");
    execAndMatch("udp bind :: 12345", "Done");
}
