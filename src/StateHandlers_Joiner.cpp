#include "LightThread.h"

// Starts the joiner process by configuring dataset and launching join
void LightThread::handleJoinerStart() {

     const LightThread::CliStep setupCommands[] = {
            {"dataset clear",                                                               ""},
            {"dataset init new",                                                            ""},
            {String("dataset panid ") + configuredPanid,                                    ""},
            {String("dataset channel ") + configuredChannel,                                ""},
            {String("dataset meshlocalprefix ") + configuredPrefix,                         ""},
            {String("dataset networkkey ") + LIGHTTHREAD_NETWORK_KEY,                       ""},
            {String("dataset networkname ") + LIGHTTHREAD_NETWORK_NAME,                     ""},
            {String("mode ") + LIGHTTHREAD_THREAD_MODE,                                     ""},
            {String("routerselectionjitter ") + LIGHTTHREAD_ROUTER_SELECTION_JITTER,        ""},
            {String("routerupgradethreshold ") + LIGHTTHREAD_ROUTER_UPGRADE_THRESHOLD,      ""},
            {String("routerdowngradethreshold ") + LIGHTTHREAD_ROUTER_DOWNGRADE_THRESHOLD,  ""},
            {"dataset commit active",                                                       ""},
            {"ifconfig up",                                                                 ""},
            {"udp close",                                                                   ""},
            {"udp open",                                                                    ""},
            {String("udp bind :: ") + LIGHTTHREAD_UDP_PORT,                                 ""},
            {"ipaddr mleid",                                                                ""}
        };     

    const LightThread::CliStep postIPCommands[] = {
            {String("joiner start ") + LIGHTTHREAD_JOINER_PSKD,         ""},
            {"thread start",                                            ""},

        };   

    static bool capturedIp = false;
    if(justEntered) {
        justEntered = false;
        resetCliSteps();
        capturedIp = false;

        logLightThread(
            LIGHTTHREAD_LOG_INFO,
            "JOINER_START: configuring dataset and starting joiner"
        );
    }

    if(!capturedIp){
        if(!runCliSteps(setupCommands, 17)) {
            return;
        }
        captureMyIpFromResponse(pendingCliResponse);
        capturedIp = true;
        resetCliSteps();

    }
    

    if(!runCliSteps(postIPCommands, 2)) {
        return;
    }

    
    setState(State::JOINER_SCAN);

}

// Checks for joiner success/failure and transitions accordingly
void LightThread::handleJoinerScan() {
    const LightThread::CliStep joinerScanCommands[] = {
            {"joiner state", "~Join failed;success|Idle"}
        };

    if(justEntered) {
        justEntered = false;
        resetCliSteps();
    }

    if(!runCliSteps(joinerScanCommands, 1)) {
        return;
    }

    setState(State::JOINER_WAIT_BROADCAST);
}

// Waits for leader’s Pairing Broadcast
void LightThread::handleJoinerWaitBroadcast() {
    if(justEntered) {
        justEntered = false;
        resetCliSteps();

        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_WAIT_BROADCAST: Listening for leader broadcast...");
    }

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
        resetCliSteps();

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
    static bool firedJoinCallback = false;
    static bool verifiedAttached = false;


    const LightThread::CliStep pairedCheckCommands[] = {
            {"state", "child|router"}

        };



    if(justEntered) {
        justEntered = false;
        resetCliSteps();

        firedJoinCallback = false;
        verifiedAttached = false;

        lastHeartbeatSent = millis();
        lastHeartbeatEcho = millis();

        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_PAIRED: Verifying Thread state...");
    }

    // Phase 1: verify we are attached
    if(!verifiedAttached) {
        if(!runCliSteps(pairedCheckCommands, 1)) {
            return;
        }

        verifiedAttached = true;
        resetCliSteps();
    }

    if(!firedJoinCallback) {
        firedJoinCallback = true;
        resetCliSteps();

        if(joinCallback) {
            String hashStr = hashToString(generateMacHash());

            joinCallback(leaderIp, hashStr);

            logLightThread(LIGHTTHREAD_LOG_INFO,
                           "JOINER_PAIRED: Fired joinCallback with IP %s and hash %s",
                           leaderIp.c_str(),
                           hashStr.c_str());
            lastHeartbeatSent = millis();
            lastHeartbeatEcho = millis();
        }
    }    

    sendHeartbeatIfDue();
}

void LightThread::handleJoinerReconnect() {

    const LightThread::CliStep reconnectCommands[] = {
                {"thread stop",                                                                 ""},
                {"ifconfig down",                                                               ""},
                {"udp close",                                                                   ""},
                {String("dataset panid ") + configuredPanid,                                    ""},
                {String("dataset channel ") + configuredChannel,                                ""},
                {String("dataset meshlocalprefix ") + configuredPrefix,                         ""},
                {String("dataset networkkey ") + LIGHTTHREAD_NETWORK_KEY,                       ""},
                {String("dataset networkname ") + LIGHTTHREAD_NETWORK_NAME,                     ""},
                {String("mode ") + LIGHTTHREAD_THREAD_MODE,                                     ""},
                {String("routerselectionjitter ") + LIGHTTHREAD_ROUTER_SELECTION_JITTER,        ""},
                {String("routerupgradethreshold ") + LIGHTTHREAD_ROUTER_UPGRADE_THRESHOLD,      ""},
                {String("routerdowngradethreshold ") + LIGHTTHREAD_ROUTER_DOWNGRADE_THRESHOLD,  ""},
                {"dataset commit active",                                                       ""},
                {"ifconfig up",                                                                 ""},
                {"udp open",                                                                    ""},
                {String("udp bind :: ") + LIGHTTHREAD_UDP_PORT,                                 ""},
                {"ipaddr mleid",                                                                ""}
            };

     const LightThread::CliStep postIPCommands[] = {
            {"thread start",    ""},
            {"state",           "child|router"}
        };   
    static bool capturedIp = false;
    if(justEntered) {
        justEntered = false;
        resetCliSteps();
        capturedIp = false;

        logLightThread(
            LIGHTTHREAD_LOG_INFO,
            "JOINER_START: configuring dataset and reconnecting joiner"
        );
    }
    if(!capturedIp){
        if(!runCliSteps(reconnectCommands, 17)) {
            return;
        }
        
        captureMyIpFromResponse(pendingCliResponse);
        capturedIp = true;
        resetCliSteps();
    }

    if(!runCliSteps(postIPCommands, 2)) {
        return;
    }

    
    setState(State::JOINER_PAIRED);
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
    const LightThread::CliStep joinerClearCommands[] = {
            {"thread stop",     ""},
            {"ifconfig down",   ""},
            {"udp close",       ""},
            {"dataset clear",   ""},
        };    

    if(justEntered) {
        justEntered = false;
        resetCliSteps();

        
        clearPersistentState();

        leaderIp = "";
        myIp = "";
        lastHeartbeatSent = 0;
        lastHeartbeatEcho = 0;

        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_FACTORY_RESET: clearing Thread state");
    }

    if(!runCliSteps(joinerClearCommands, 4)) {
        return;
    }
    
    setState(State::STANDBY);
}
