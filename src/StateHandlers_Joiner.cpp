#include "LightThread.h"

String LightThread::getJoinerSetupCommand(int step) {
    switch(step) {
        case 0:  return "dataset clear";
        case 1:  return "dataset init new";
        case 2:  return "dataset panid " + configuredPanid;
        case 3:  return "dataset channel " + String(configuredChannel);
        case 4:  return "dataset meshlocalprefix " + configuredPrefix;
        case 5:  return "dataset networkkey 00112233445566778899aabbccddeeff";
        case 6:  return "dataset networkname OpenThreadMesh";
        case 7:  return "mode rn";
        case 8:  return "routerselectionjitter 0";
        case 9:  return "routerupgradethreshold 255";
        case 10: return "routerdowngradethreshold 1";
        case 11: return "dataset commit active";
        case 12: return "ifconfig up";
        case 13: return "udp close";
        case 14: return "udp open";
        case 15: return "udp bind :: 12345";
        case 16: return "ipaddr mleid";
        default: return "";
    }
}

bool LightThread::runJoinerSetupSequence(int &step, const char *logPrefix) {
    const int commandCount = getJoinerSetupCommandCount();

    if(cliCommandFailed()) {
        cliFailed = false;
        logLightThread(LT_LOG_ERROR, "%s: CLI command failed at setup step %d", logPrefix, step);
        setState(State::ERROR);
        return false;
    }

    if(cliCommandDone()) {
        cliDone = false;

        String command = getJoinerSetupCommand(step);

        if(command == "ipaddr mleid") {
            captureMyIpFromResponse(getCliResponse());
        }

        step++;

        if(step >= commandCount) {
            return true;
        }
    }

    if(!cliBusy) {
        String command = getJoinerSetupCommand(step);
        String expected = (command == "ipaddr mleid") ? "" : "Done";
        startCliCommand(command, expected, 3000);
    }

    return false;
}

// Starts the joiner process by configuring dataset and launching join
void LightThread::handleJoinerStart() {
    enum JoinerStartStep {
        RUN_SETUP,
        START_JOINER,
        START_THREAD
    };
    

    static JoinerStartStep startStep = RUN_SETUP;
    static int setupStep = 0;

    if(justEntered) {
        justEntered = false;

        startStep = RUN_SETUP;
        setupStep = 0;

        logLightThread(LT_LOG_INFO, "JOINER_START: Configuring dataset and starting joiner...");
    }

    if(cliCommandFailed()) {
        cliFailed = false;
        logLightThread(LT_LOG_ERROR, "JOINER_START: CLI command failed");
        setState(State::ERROR);
        return;
    }

    switch(startStep) {
        case RUN_SETUP:
            if(runJoinerSetupSequence(setupStep, "JOINER_START")) {
                logLightThread(LT_LOG_INFO, "JOINER_START: setup complete, starting joiner");
                startStep = START_JOINER;
            }
            return;

        case START_JOINER:
            if(cliCommandDone()) {
                cliDone = false;
                startStep = START_THREAD;
                return;
            }

            if(!cliBusy) {
                startCliCommand("joiner start J01NME", "Done", 3000);
            }
            return;

        case START_THREAD:
            if(cliCommandDone()) {
                cliDone = false;
                logLightThread(LT_LOG_INFO, "JOINER_START: setup complete, scanning joiner state");
                setState(State::JOINER_SCAN);
                return;
            }

            if(!cliBusy) {
                startCliCommand("thread start", "Done", 3000);
            }
            return;
    }
}

// Checks for joiner success/failure and transitions accordingly
void LightThread::handleJoinerScan() {
    static unsigned long lastCheck = 0;

    if(justEntered) {
        justEntered = false;
        lastCheck = 0;
        logLightThread(LT_LOG_INFO, "JOINER_SCAN: checking joiner state...");
    }


    if(cliCommandFailed()){
        cliFailed = false;
        logLightThread(LT_LOG_WARN, "JOINER_SCAN: Failed to get joiner state");
        lastCheck = timeInState();
        return;
    }

    if(cliCommandDone()){
        cliDone = false;
        String response = getCliResponse();
        logLightThread(LT_LOG_INFO, "JOINER state response: %s", response.c_str());

        if(response.indexOf("Join failed") == -1 && (response.indexOf("success") != -1 || response.indexOf("Idle") != -1)){
            logLightThread(LT_LOG_INFO, "JOINER_SCAN: Joiner successfully paired");
            setState(State::JOINER_WAIT_BROADCAST);
            return;
        }
        lastCheck = timeInState();
        return;
    }

    if(!cliBusy && timeInState() - lastCheck >= 1000){
        startCliCommand("joiner state", "", 2000);
    }
}

// Waits for leader’s Pairing Broadcast
void LightThread::handleJoinerWaitBroadcast() {
    if(justEntered) {
        justEntered = false;
        logLightThread(LT_LOG_INFO, "JOINER_WAIT_BROADCAST: Listening for leader broadcast...");
    }

    if(!inState(State::JOINER_WAIT_BROADCAST))
        return;

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
        logLightThread(LT_LOG_INFO, "JOINER_WAIT_ACK: Waiting for PAIRING_RESPONSE...");
    }

    if(timeInState() > 10000) { // 10s timeout
        logLightThread(LT_LOG_WARN, "JOINER_WAIT_ACK: Timed out waiting for ACK");
        setState(State::STANDBY);
    }
}

// Fully paired state — fires join callback once, waits for attach to settle,
// then optionally escalates to rdn mode.
void LightThread::handleJoinerPaired() {
    enum PairedStep {
        CHECK_STATE,
        WAIT_BEFORE_MODE_CHECK,
        CHECK_MODE,
        SET_MODE_RDN,
        READY
    };

    static PairedStep step = CHECK_STATE;
    static unsigned long lastStateQueryTime = 0;
    static unsigned long attachedTime = 0;
    static bool firedJoinCallback = false;

    const unsigned long stateQueryIntervalMs = 1000;
    const unsigned long modeEscalationDelayMs = 5000;

    if(justEntered) {
        justEntered = false;

        step = CHECK_STATE;
        lastStateQueryTime = 0;
        attachedTime = 0;
        firedJoinCallback = false;

        lastHeartbeatSent = millis();
        lastHeartbeatEcho = millis();

        logLightThread(LT_LOG_INFO, "JOINER_PAIRED: Verifying Thread state...");
    }

    if(cliCommandFailed()) {
        cliFailed = false;

        logLightThread(LT_LOG_WARN, "JOINER_PAIRED: CLI command failed, retrying state check");

        step = CHECK_STATE;
        lastStateQueryTime = 0;
        return;
    }

    switch(step) {
        case CHECK_STATE:
            if(cliCommandDone()) {
                cliDone = false;

                String stateResp = getCliResponse();
                stateResp.toLowerCase();

                if(stateResp.indexOf("child") != -1 || stateResp.indexOf("router") != -1) {
                    logLightThread(LT_LOG_INFO,
                                   "JOINER_PAIRED: Attached: %s",
                                   stateResp.c_str());

                    if(!firedJoinCallback) {
                        firedJoinCallback = true;

                        if(joinCallback) {
                            uint64_t myHash = generateMacHash();
                            String hashStr = String((uint32_t)(myHash >> 32), HEX) +
                                             String((uint32_t)(myHash & 0xFFFFFFFF), HEX);

                            joinCallback(leaderIp, hashStr);

                            logLightThread(LT_LOG_INFO,
                                           "JOINER_PAIRED: Fired joinCallback with IP %s and hash %s",
                                           leaderIp.c_str(),
                                           hashStr.c_str());
                        }
                    }

                    attachedTime = millis();
                    step = WAIT_BEFORE_MODE_CHECK;
                    return;
                }

                logLightThread(LT_LOG_WARN,
                               "JOINER_PAIRED: Not attached yet: %s",
                               stateResp.c_str());

                lastStateQueryTime = millis();
                return;
            }

            if(!cliBusy && millis() - lastStateQueryTime >= stateQueryIntervalMs) {
                lastStateQueryTime = millis();
                startCliCommand("state", "", 1000);
            }
            return;

        case WAIT_BEFORE_MODE_CHECK:
            if(millis() - attachedTime < modeEscalationDelayMs) {
                return;
            }

            logLightThread(LT_LOG_INFO,
                           "JOINER_PAIRED: Checking mode after attach settle delay");

            step = CHECK_MODE;
            startCliCommand("mode", "r", 1000);
            return;

        case CHECK_MODE:
            if(!cliCommandDone()) return;

            {
                cliDone = false;

                String modeResp = getCliResponse();
                modeResp.toLowerCase();

                if(modeResp.indexOf("d") == -1) {
                    logLightThread(LT_LOG_INFO, "JOINER_PAIRED: Setting mode rdn");

                    step = SET_MODE_RDN;
                    startCliCommand("mode rdn", "Done", 1000);
                } else {
                    logLightThread(LT_LOG_INFO, "JOINER_PAIRED: Already in rdn mode");

                    step = READY;
                    lastHeartbeatSent = millis();
                    lastHeartbeatEcho = millis();
                }
            }
            return;

        case SET_MODE_RDN:
            if(!cliCommandDone()) return;

            cliDone = false;

            logLightThread(LT_LOG_INFO, "JOINER_PAIRED: mode rdn set");

            step = READY;
            lastHeartbeatSent = millis();
            lastHeartbeatEcho = millis();
            return;

        case READY:
            break;
    }

    // Re-enable after the rdn crash is confirmed fixed.
    sendHeartbeatIfDue();
}

// Attempt to reconnect to last known leader
void LightThread::handleJoinerReconnect() {
    enum ReconnectStep {
        RUN_SETUP,
        START_THREAD,
        WAIT_FOR_ATTACH
    };

    static ReconnectStep reconnectStep = RUN_SETUP;
    static int setupStep = 0;
    static unsigned long lastStateCheckTime = 0;

    if(justEntered) {
        justEntered = false;

        reconnectStep = RUN_SETUP;
        setupStep = 0;
        lastStateCheckTime = 0;

        lastHeartbeatSent = millis();
        lastHeartbeatEcho = millis();

        logLightThread(LT_LOG_INFO, "JOINER_RECONNECT: configuring dataset and bringing up stack");
    }

    if(cliCommandFailed()) {
        cliFailed = false;

        logLightThread(LT_LOG_WARN, "JOINER_RECONNECT: CLI command failed");
        setState(State::STANDBY);
        return;
    }

    switch(reconnectStep) {
        case RUN_SETUP:
            if(runJoinerSetupSequence(setupStep, "JOINER_RECONNECT")) {
                reconnectStep = START_THREAD;
                logLightThread(LT_LOG_INFO, "JOINER_RECONNECT: setup complete, starting Thread");
            }
            return;

        case START_THREAD:
            if(cliCommandDone()) {
                cliDone = false;
                reconnectStep = WAIT_FOR_ATTACH;
                lastStateCheckTime = 0;

                logLightThread(LT_LOG_INFO, "JOINER_RECONNECT: waiting for attach");
                return;
            }

            if(!cliBusy) {
                startCliCommand("thread start", "Done", 3000);
            }
            return;

        case WAIT_FOR_ATTACH:
            if(cliCommandDone()) {
                cliDone = false;

                String resp = getCliResponse();
                resp.toLowerCase();

                if(resp.indexOf("child") != -1 || resp.indexOf("router") != -1) {
                    logLightThread(LT_LOG_INFO,
                                   "JOINER_RECONNECT: back in mesh as %s",
                                   resp.c_str());

                    setState(State::JOINER_PAIRED);
                    return;
                }

                logLightThread(LT_LOG_INFO,
                               "JOINER_RECONNECT: not attached yet: %s",
                               resp.c_str());
            }

            if(!cliBusy && millis() - lastStateCheckTime > 2000) {
                lastStateCheckTime = millis();
                startCliCommand("state", "", 1000);
            }

            if(timeInState() > 120000) {
                logLightThread(LT_LOG_WARN,
                               "JOINER_RECONNECT: Timeout - going to standby");

                setState(State::STANDBY);
            }
            return;
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
        std::vector<uint8_t> payload = hashToBytes(generateMacHash());

        sendUdpPacket(MessageType::RECONNECT_REQUEST, payload, "ff03::1", 12345);
        lastHeartbeatSent = millis(); // Rate-limit retries
        setState(State::JOINER_SEEKING_LEADER);
        return;
    }

    lastHeartbeatSent = millis();

    // Normal heartbeat to known leader IP
    std::vector<uint8_t> payload = hashToBytes(generateMacHash());

    bool ok = sendUdpPacket(MessageType::HEARTBEAT, payload, leaderIp, 12345);
    if(ok) {
        logLightThread(LT_LOG_INFO, "HEARTBEAT: Sent to leader");
    } else {
        logLightThread(LT_LOG_WARN, "HEARTBEAT: Failed to send");
    }
}
