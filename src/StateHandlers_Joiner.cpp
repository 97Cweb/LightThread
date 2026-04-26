#include "LightThread.h"

// Starts the joiner process by configuring dataset and launching join
void LightThread::handleJoinerStart() {
    static int step = 0;

    step = 0;
    
    logLightThread(LT_LOG_INFO, "JOINER_START: Configuring dataset and starting joiner...");

    const String commands[] = {
        "dataset clear",
        "dataset init new",
        "dataset panid " + configuredPanid,
        "dataset channel " + String(configuredChannel),
        "dataset meshlocalprefix " + configuredPrefix,
        "dataset networkkey 00112233445566778899aabbccddeeff",
        "dataset networkname OpenThreadMesh",

        "mode rn",
        "routerselectionjitter 0",
        "routerupgradethreshold 255",
        "routerdowngradethreshold 1",
        "dataset commit active",
        "ifconfig up",
        "udp close",
        "udp open",
        "udp bind :: 12345",

        "ipaddr mleid",

        "joiner start J01NME",
        "thread start"
    };

    const int commandCount = sizeof(commands)/sizeof(commands[0]);

    if(cliCommandFailed()){
        cliFailed = false;
        logLightThread(LT_LOG_ERROR,"JOINER_START: CLI command failed at step %d", step);
        setState(State::ERROR);
        return;
    }

    if(cliCommandDone()){
        cliDone = false;

        if(commands[step] == "ipaddr mleid"){
            captureMyIpFromResponse(getCliResponse());
        }        

        step++;
        
        if(step>= commandCount){
            logLightThread(LT_LOG_INFO, "JOINER_START: setup complete, scanning joiner state");
            setState(State::JOINER_SCAN);
            return;
        }
    }
    if(!cliBusy){
        startCliCommand(commands[step],"Done",3000);
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

// Waits for leader’s WHOAMI broadcast
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
        logLightThread(LT_LOG_INFO, "JOINER_WAIT_ACK: Waiting for PAIR_ACK...");
    }

    if(timeInState() > 10000) { // 10s timeout
        logLightThread(LT_LOG_WARN, "JOINER_WAIT_ACK: Timed out waiting for ACK");
        setState(State::STANDBY);
    }
}

// Fully paired state — sends heartbeat, escalates if needed
void LightThread::handleJoinerPaired() {

    enum PairedStep{
        CHECK_STATE,
        CHECK_MODE,
        SET_MODE_RDN,
        READY
    };

    static PairedStep step = CHECK_STATE;
    static unsigned long lastCheck = 0;

    static bool firedJoinCallback = false;

    if(justEntered) {
        justEntered = false;
        firedJoinCallback = false;
        step = CHECK_STATE;
        lastCheck = millis(); 
        logLightThread(LT_LOG_INFO, "JOINER_PAIRED: Verifying Thread state...");


    }
    if(cliCommandFailed()){
        cliFailed = false;
        logLightThread(LT_LOG_WARN, "JOINER_PAIRED: CLI command failed, retrying");
        step = CHECK_STATE;
        lastCheck = timeInState();
        return;
    }   

    switch(step){
        case CHECK_STATE:
            if(cliCommandDone()){
                cliDone = false;

                String stateResp = getCliResponse();

                if(stateResp.indexOf("child") != -1 || stateResp.indexOf("rouer") != -1){
                    logLightThread(LT_LOG_INFO,"JOINER_PAIRED: Attached: %s", stateResp.c_str());

                    if(joinCallback) {
                        uint64_t myHash = generateMacHash();
                        String hashStr = String((uint32_t)(myHash >> 32), HEX) +
                                         String((uint32_t)(myHash & 0xFFFFFFFF), HEX);
                        joinCallback(leaderIp, hashStr);
                        logLightThread(LT_LOG_INFO, "JOINER_PAIRED: Fired joinCallback with IP %s and hash %s",
                                       leaderIp.c_str(), hashStr.c_str());
                    }

                    step = CHECK_MODE;
                    startCliCommand("mode", "", 1000);
                }
                else{
                    logLightThread(LT_LOG_WARN, "JOINER_PAIRED: Not attached: %s", stateResp.c_str());
                    lastCheck = timeInState();
                }
                return;
            }
            if(!cliBusy && timeInState() - lastCheck >=1000){
                lastCheck = timeInState();
                startCliCommand("state", "",1000);
            }
            return;
        case CHECK_MODE:
            if(!cliCommandDone()) return;
            
            {
                cliDone = false;

                String modeResp = getCliResponse();

                if(modeResp.indexOf("rdn") == -1){
                    logLightThread(LT_LOG_INFO, "JOINER_PAIRED: setting mode rdn");
                    step = SET_MODE_RDN;
                    startCliCommand("mode rdn", "Done", 1000);
                }
                else{
                    logLightThread(LT_LOG_INFO, "JOINER_PAIRED: Ready");
                    step = READY;
                }
            }
            return;
        case SET_MODE_RDN:
            if(!cliCommandDone()) return;
            
            cliDone = false;
            logLightThread(LT_LOG_INFO, "JOINER_PAIRED: mode rdn set");
            step = READY;
            return;
        case READY:
            break;
    }

    sendHeartbeatIfDue();

}

// Attempt to reconnect to last known leader
void LightThread::handleJoinerReconnect() {
    static unsigned long lastCheck = 0;
    static bool threadStarted = false;

    if(justEntered) {
        justEntered = false;
        logLightThread(LT_LOG_INFO, "JOINER_RECONNECT: bringing up stack for auto-heal");

        threadStarted = false;
        lastHeartbeatSent = 0;
        lastHeartbeatEcho = 0;
        lastCheck = 0;

        startCliCommand("thread start", "Done", 3000);
        return;

    }        

    if(cliCommandFailed()){
        cliFailed = false;
        logLightThread(LT_LOG_WARN, "JOINER_RECONNECT: CLI command failed");
        setState(State::STANDBY);
        return;
    }

    if(!threadStarted){
        if(!cliCommandDone()) return;

        cliDone = false;
        threadStarted = true;
        logLightThread(LT_LOG_INFO, "JOINER_RECONNECT: thread start accepted");
    }


    
    sendHeartbeatIfDue();

    if(cliCommandDone()){
        cliDone = false;
        String resp = getCliResponse();
        resp.toLowerCase();

        if(resp.indexOf("child") !=-1 || resp.indexOf("router") != -1){
            logLightThread(LT_LOG_INFO, "JOINER_RECOONECT: back in mesh as %s", resp.c_str());
            setState(State::JOINER_PAIRED);
            return;
        }        
        logLightThread(LT_LOG_INFO, "JOINER_RECONNECT: not attached yet: %s", resp.c_str());
        return;
    }

    if(!cliBusy && millis() -lastCheck >2000){
        lastCheck = millis();
        startCliCommand("state", "", 1000);
    }

    if(timeInState() > 120000){
        logLightThread(LT_LOG_WARN, "JOINER_RECONNECT: Timeout - going to standby");
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

        sendUdpPacket(MessageType::RECONNECT_REQUEST, payload, "ff03::1", 12345);
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

    bool ok = sendUdpPacket(MessageType::HEARTBEAT, payload, leaderIp, 12345);
    if(ok) {
        logLightThread(LT_LOG_INFO, "HEARTBEAT: Sent to leader");
    } else {
        logLightThread(LT_LOG_WARN, "HEARTBEAT: Failed to send");
    }
}
