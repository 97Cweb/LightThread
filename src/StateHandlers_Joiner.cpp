#include "LightThread.h"

// Starts the joiner process by configuring dataset and launching join
void LightThread::handleJoinerStart() {
    enum JoinerStartStep {
        RUN_SETUP,
        START_JOINER,
        START_THREAD
    };

    static JoinerStartStep startStep = RUN_SETUP;
    static int setupStep = 0;

    const String setupCommands[] = {
        "dataset clear",
        "dataset init new",
        String("dataset panid ") + configuredPanid,
        String("dataset channel ") + configuredChannel,
        String("dataset meshlocalprefix ") + configuredPrefix,
        String("dataset networkkey ") + LIGHTTHREAD_NETWORK_KEY,
        String("dataset networkname ") + LIGHTTHREAD_NETWORK_NAME,
        String("mode ") + LIGHTTHREAD_THREAD_MODE,
        String("routerselectionjitter ") + LIGHTTHREAD_ROUTER_SELECTION_JITTER,
        String("routerupgradethreshold ") + LIGHTTHREAD_ROUTER_UPGRADE_THRESHOLD,
        String("routerdowngradethreshold ") + LIGHTTHREAD_ROUTER_DOWNGRADE_THRESHOLD,
        "dataset commit active",
        "ifconfig up",
        "udp close",
        "udp open",
        String("udp bind :: ") + LIGHTTHREAD_UDP_PORT,
        LIGHTTHREAD_CLI_MLEID_COMMAND
    };

    const int setupCommandCount = commandCountFromBytes(setupCommands, sizeof(setupCommands));

    if(justEntered) {
        justEntered = false;

        startStep = RUN_SETUP;
        setupStep = 0;

        logLightThread(
            LIGHTTHREAD_LOG_INFO,
            "JOINER_START: configuring dataset and starting joiner"
        );
    }

    switch(startStep) {
        case RUN_SETUP:
            if(runCliCommandList(setupCommands, setupCommandCount, setupStep, "JOINER_START")) {
                logLightThread(
                    LIGHTTHREAD_LOG_INFO,
                    "JOINER_START: setup complete, starting joiner"
                );

                startStep = START_JOINER;
            }
            return;

        case START_JOINER:
            if(cliCommandFailed()) {
                cliFailed = false;
                logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER_START: joiner start failed");
                setState(State::ERROR);
                return;
            }

            if(cliCommandDone()) {
                cliDone = false;
                startStep = START_THREAD;
                return;
            }

            if(!cliBusy) {
                startCliCommand(
                    String("joiner start ") + LIGHTTHREAD_JOINER_PSKD,
                    LIGHTTHREAD_CLI_DONE,
                    LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS
                );
            }
            return;

        case START_THREAD:
            if(cliCommandFailed()) {
                cliFailed = false;
                logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER_START: thread start failed");
                setState(State::ERROR);
                return;
            }

            if(cliCommandDone()) {
                cliDone = false;

                logLightThread(
                    LIGHTTHREAD_LOG_INFO,
                    "JOINER_START: thread started, scanning joiner state"
                );

                setState(State::JOINER_SCAN);
                return;
            }

            if(!cliBusy) {
                startCliCommand(
                    "thread start",
                    LIGHTTHREAD_CLI_DONE,
                    LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS
                );
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
        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_SCAN: checking joiner state...");
    }


    if(cliCommandFailed()){
        cliFailed = false;
        logLightThread(LIGHTTHREAD_LOG_WARN, "JOINER_SCAN: Failed to get joiner state");
        lastCheck = timeInState();
        return;
    }

    if(cliCommandDone()){
        cliDone = false;
        String response = getCliResponse();
        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER state response: %s", response.c_str());

        if(response.indexOf("Join failed") == -1 && (response.indexOf("success") != -1 || response.indexOf("Idle") != -1)){
            logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_SCAN: Joiner successfully paired");
            setState(State::JOINER_WAIT_BROADCAST);
            return;
        }
        lastCheck = timeInState();
        return;
    }

    if(!cliBusy && timeInState() - lastCheck >= LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS){
        startCliCommand("joiner state", "", LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);
    }
}

// Waits for leader’s Pairing Broadcast
void LightThread::handleJoinerWaitBroadcast() {
    if(justEntered) {
        justEntered = false;
        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_WAIT_BROADCAST: Listening for leader broadcast...");
    }

    if(!inState(State::JOINER_WAIT_BROADCAST))
        return;

    // Timeout fallback
    if(millis() - stateEntryTime > LIGHTTHREAD_JOINER_START_TIMEOUT_MS) {
        logLightThread(LIGHTTHREAD_LOG_WARN, "JOINER_WAIT_BROADCAST: Timed out waiting for broadcast.");
        setState(State::STANDBY);
        return;
    }
}

// Waits for leader to acknowledge our response
void LightThread::handleJoinerWaitAck() {
    if(justEntered) {
        justEntered = false;
        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_WAIT_RESPONSE: Waiting for PAIRING_RESPONSE...");
    }

    if(timeInState() > LIGHTTHREAD_JOINER_SCAN_TIMEOUT_MS) { // 10s timeout
        logLightThread(LIGHTTHREAD_LOG_WARN, "JOINER_WAIT_RESPONSE: Timed out waiting for ACK");
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


    if(justEntered) {
        justEntered = false;

        step = CHECK_STATE;
        lastStateQueryTime = 0;
        attachedTime = 0;
        firedJoinCallback = false;

        lastHeartbeatSent = millis();
        lastHeartbeatEcho = millis();

        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_PAIRED: Verifying Thread state...");
    }

    if(cliCommandFailed()) {
        cliFailed = false;

        logLightThread(LIGHTTHREAD_LOG_WARN, "JOINER_PAIRED: CLI command failed, retrying state check");

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
                    logLightThread(LIGHTTHREAD_LOG_INFO,
                                   "JOINER_PAIRED: Attached: %s",
                                   stateResp.c_str());

                    if(!firedJoinCallback) {
                        firedJoinCallback = true;

                        if(joinCallback) {
                            String hashStr = hashToString(generateMacHash());

                            joinCallback(leaderIp, hashStr);

                            logLightThread(LIGHTTHREAD_LOG_INFO,
                                           "JOINER_PAIRED: Fired joinCallback with IP %s and hash %s",
                                           leaderIp.c_str(),
                                           hashStr.c_str());
                        }
                    }

                    attachedTime = millis();
                    step = WAIT_BEFORE_MODE_CHECK;
                    return;
                }

                logLightThread(LIGHTTHREAD_LOG_WARN,
                               "JOINER_PAIRED: Not attached yet: %s",
                               stateResp.c_str());

                lastStateQueryTime = millis();
                return;
            }

            if(!cliBusy && millis() - lastStateQueryTime >= LIGHTTHREAD_STATE_CHECK_INTERVAL_MS) {
                lastStateQueryTime = millis();
                startCliCommand("state", "", LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);
            }
            return;

        case WAIT_BEFORE_MODE_CHECK:
            if(millis() - attachedTime < LIGHTTHREAD_MODE_ESCALATION_DELAY_MS) {
                return;
            }

            logLightThread(LIGHTTHREAD_LOG_INFO,
                           "JOINER_PAIRED: Checking mode after attach settle delay");

            step = CHECK_MODE;
            startCliCommand("mode", "r", LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);
            return;

        case CHECK_MODE:
            if(!cliCommandDone()) return;

            {
                cliDone = false;

                String modeResp = getCliResponse();
                modeResp.toLowerCase();

                if(modeResp.indexOf("d") == -1) {
                    logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_PAIRED: Setting mode rdn");

                    step = SET_MODE_RDN;
                    startCliCommand("mode rdn", LIGHTTHREAD_CLI_DONE, LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);
                } else {
                    logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_PAIRED: Already in rdn mode");

                    step = READY;
                    lastHeartbeatSent = millis();
                    lastHeartbeatEcho = millis();
                }
            }
            return;

        case SET_MODE_RDN:
            if(!cliCommandDone()) return;

            cliDone = false;

            logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_PAIRED: mode rdn set");

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

void LightThread::handleJoinerReconnect() {
    enum ReconnectStep {
        RUN_SETUP,
        START_THREAD,
        WAIT_FOR_ATTACH
    };

    static ReconnectStep reconnectStep = RUN_SETUP;
    static int setupStep = 0;
    static unsigned long lastStateCheckTime = 0;

    const String setupCommands[] = {
        "thread stop",
        "ifconfig down",
        "udp close",
        String("dataset panid ") + configuredPanid,
        String("dataset channel ") + configuredChannel,
        String("dataset meshlocalprefix ") + configuredPrefix,
        String("dataset networkkey ") + LIGHTTHREAD_NETWORK_KEY,
        String("dataset networkname ") + LIGHTTHREAD_NETWORK_NAME,
        String("mode ") + LIGHTTHREAD_THREAD_MODE,
        String("routerselectionjitter ") + LIGHTTHREAD_ROUTER_SELECTION_JITTER,
        String("routerupgradethreshold ") + LIGHTTHREAD_ROUTER_UPGRADE_THRESHOLD,
        String("routerdowngradethreshold ") + LIGHTTHREAD_ROUTER_DOWNGRADE_THRESHOLD,
        "dataset commit active",
        "ifconfig up",
        "udp open",
        String("udp bind :: ") + LIGHTTHREAD_UDP_PORT,
        LIGHTTHREAD_CLI_MLEID_COMMAND
    };

    const int setupCommandCount = commandCountFromBytes(setupCommands, sizeof(setupCommands));

    if(justEntered) {
        justEntered = false;

        reconnectStep = RUN_SETUP;
        setupStep = 0;
        lastStateCheckTime = 0;

        lastHeartbeatSent = millis();
        lastHeartbeatEcho = millis();

        logLightThread(
            LIGHTTHREAD_LOG_INFO,
            "JOINER_RECONNECT: configuring dataset and bringing up stack"
        );
    }

    switch(reconnectStep) {
        case RUN_SETUP:
            if(runCliCommandList(setupCommands, setupCommandCount, setupStep, "JOINER_RECONNECT")) {
                reconnectStep = START_THREAD;

                logLightThread(
                    LIGHTTHREAD_LOG_INFO,
                    "JOINER_RECONNECT: setup complete, starting Thread"
                );
            }
            return;

        case START_THREAD:
            if(cliCommandFailed()) {
                cliFailed = false;

                logLightThread(
                    LIGHTTHREAD_LOG_WARN,
                    "JOINER_RECONNECT: thread start failed"
                );

                setState(State::STANDBY);
                return;
            }

            if(cliCommandDone()) {
                cliDone = false;

                reconnectStep = WAIT_FOR_ATTACH;
                lastStateCheckTime = 0;

                logLightThread(
                    LIGHTTHREAD_LOG_INFO,
                    "JOINER_RECONNECT: waiting for attach"
                );
                return;
            }

            if(!cliBusy) {
                startCliCommand(
                    "thread start",
                    LIGHTTHREAD_CLI_DONE,
                    LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS
                );
            }
            return;

        case WAIT_FOR_ATTACH:
            if(cliCommandFailed()) {
                cliFailed = false;

                logLightThread(
                    LIGHTTHREAD_LOG_WARN,
                    "JOINER_RECONNECT: failed to get state"
                );

                lastStateCheckTime = millis();
                return;
            }

            if(cliCommandDone()) {
                cliDone = false;

                String resp = getCliResponse();
                resp.toLowerCase();

                if(resp.indexOf("child") != -1 || resp.indexOf("router") != -1) {
                    logLightThread(
                        LIGHTTHREAD_LOG_INFO,
                        "JOINER_RECONNECT: back in mesh as %s",
                        resp.c_str()
                    );

                    setState(State::JOINER_PAIRED);
                    return;
                }

                logLightThread(
                    LIGHTTHREAD_LOG_INFO,
                    "JOINER_RECONNECT: not attached yet: %s",
                    resp.c_str()
                );
            }

            if(!cliBusy && millis() - lastStateCheckTime >= LIGHTTHREAD_STATE_CHECK_INTERVAL_MS) {
                lastStateCheckTime = millis();

                startCliCommand(
                    "state",
                    "",
                    LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS
                );
            }

            if(timeInState() > LIGHTTHREAD_JOINER_RECONNECT_TIMEOUT_MS) {
                logLightThread(
                    LIGHTTHREAD_LOG_WARN,
                    "JOINER_RECONNECT: timeout, going to standby"
                );

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
    if(millis() - lastHeartbeatSent < LIGHTTHREAD_HEARTBEAT_INTERVAL_MS)
        return;

    // No echo in 15s → assume leader is dead and trigger reconnect
    if(millis() - lastHeartbeatEcho > LIGHTTHREAD_HEARTBEAT_TIMEOUT_MS) {
        logLightThread(LIGHTTHREAD_LOG_WARN, "HEARTBEAT: Leader not responding. Broadcasting reconnect.");

        // Send RECONNECT request over multicast with own hashMAC
        std::vector<uint8_t> payload = hashToBytes(generateMacHash());

        sendUdpPacket(MessageType::RECONNECT_REQUEST, payload, LIGHTTHREAD_MULTICAST_ALL_NODES, LIGHTTHREAD_UDP_PORT);
        lastHeartbeatSent = millis(); // Rate-limit retries
        setState(State::JOINER_SEEKING_LEADER);
        return;
    }

    lastHeartbeatSent = millis();

    // Normal heartbeat to known leader IP
    std::vector<uint8_t> payload = hashToBytes(generateMacHash());

    bool ok = sendUdpPacket(MessageType::HEARTBEAT, payload, leaderIp, LIGHTTHREAD_UDP_PORT);
    if(ok) {
        logLightThread(LIGHTTHREAD_LOG_INFO, "HEARTBEAT: Sent to leader");
    } else {
        logLightThread(LIGHTTHREAD_LOG_WARN, "HEARTBEAT: Failed to send");
    }
}

void LightThread::handleJoinerFactoryReset() {
    static int step = 0;

    const String commands[] = {
        "thread stop",
        "ifconfig down",
        "udp close",
        "dataset clear"
    };

    const int commandCount = sizeof(commands) / sizeof(commands[0]);

    if(justEntered) {
        justEntered = false;
        step = 0;

        clearPersistentState();

        leaderIp = "";
        myIp = "";
        lastHeartbeatSent = 0;
        lastHeartbeatEcho = 0;

        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_FACTORY_RESET: clearing Thread state");
    }

    if(cliCommandFailed()) {
        cliFailed = false;

        logLightThread(
            LIGHTTHREAD_LOG_WARN,
            "JOINER_FACTORY_RESET: command failed at step %d, continuing",
            step
        );

        step++;
    }

    if(cliCommandDone()) {
        cliDone = false;
        step++;
    }

    if(step >= commandCount) {
        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_FACTORY_RESET: complete");
        setState(State::STANDBY);
        return;
    }

    if(!cliBusy) {
        startCliCommand(commands[step], LIGHTTHREAD_CLI_DONE, LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);
    }
}
