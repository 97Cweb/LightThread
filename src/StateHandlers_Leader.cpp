#include "LightThread.h"

// Waits for the Thread network to come up and become a leader or router.
// Once stable, binds the UDP socket and transitions to STANDBY.
void LightThread::handleLeaderWaitNetwork() {

    enum LeaderWaitStep{
        QUERY_STATE,
        WAIT_STATE_RESPONSE,
        UDP_OPEN,
        UDP_BIND,
        GET_MLEID,
        DONE
    };
    static LeaderWaitStep step = QUERY_STATE;

    static unsigned long lastCheck = 0;

    if(justEntered) {
        justEntered = false;
        step = QUERY_STATE;
        lastCheck = 0;
        logLightThread(LT_LOG_INFO, "LEADER_WAIT_NETWORK: Waiting for Thread network...");
    }

    if(timeInState() > 50000){
        logLightThread(LT_LOG_ERROR, "LEADER_WAIT_NETWORK: Timed out waiting for leader state");
        setState(State::ERROR);
        return;
    }

    if(cliCommandFailed()){
        logLightThread(LT_LOG_WARN, "LEADER_WAIT_NETWORK: CLI command failed");
        cliFailed = false;
        cliDone = false;
        cliBusy = false;
        step = QUERY_STATE;
        return;
    }

    switch(step){
        case QUERY_STATE:
            if(timeInState()-lastCheck < 5000) return;
            lastCheck = timeInState();
            if(startCliCommand("state","",1000)){
                step = WAIT_STATE_RESPONSE;
            }
            break;

        case WAIT_STATE_RESPONSE: {
            if(!cliCommandDone()) return;

            String response = getCliResponse();
            cliDone = false;
            if(response.indexOf("leader") != -1 || response.indexOf("router") != -1){
                logLightThread(LT_LOG_INFO,
                                "LEADER_WAIT_NETWORK: Thread is up in state: %s",
                                response.c_str());
                startCliCommand("udp open", "Done", 1000);
                step = UDP_OPEN;
            }
            else{
                logLightThread(LT_LOG_INFO, "LEADER_WAIT_NETWORK: Not leader/router yet");
                step = QUERY_STATE;
            }
            break;
        }
        case UDP_OPEN:
            if(!cliCommandDone()) return;
            
            cliDone = false;
            startCliCommand("udp bind :: 12345", "Done",1000);

            step = UDP_BIND;
            break;
        case UDP_BIND:
            if(!cliCommandDone()) return;

            cliDone = false;
            startCliCommand("ipaddr mleid", "",1000);
            step = GET_MLEID;
            break;

        case GET_MLEID:
            if(!cliCommandDone()) return;
            
            cliDone = false;
            captureMyIpFromResponse(getCliResponse());

            logLightThread(LT_LOG_INFO, "LEADER_WAIT_NETWORK: UDP ready");
            setState(State::STANDBY);
            step = DONE;
            break;
        case DONE:
            break;
    }
}

// Begins the commissioner role and adds a wildcard joiner filter.
void LightThread::handleCommissionerStart() {
    if(justEntered) {
        justEntered = false;
        logLightThread(LT_LOG_INFO, "COMMISSIONER_START: starting commissioner...");

        if(!startCliCommand("commissioner start", "Done", 3000)){
            logLightThread(LT_LOG_WARN, "COMMISSIONER_START: CLI busy");
            return;
        }
    }
    
    if(cliCommandDone()){
        cliDone = false;
        logLightThread(LT_LOG_INFO, "COMMISSIONER_START: commissioner started");
        setState(State::COMMISSIONER_ACTIVE);
        return;
    }
    
    if(cliCommandFailed()){
        cliFailed = false;
        logLightThread(LT_LOG_ERROR, "COMMISSIONER_START: Failed to start commissioner");
        setState(State::ERROR);
        return;
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
        bool ok = sendUdpPacket(MessageType::PAIRING_BROADCAST, emptyPayload,
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
                   "COMMISSIONER_ACTIVE: Pairing timed out. Stopping commissioner");
        setState(State::COMMISSIONER_STOPPING);
        return;
    }
}

void LightThread::handleCommissionerStopping(){
    if(justEntered){
        justEntered = false;
        logLightThread(LT_LOG_INFO, "COMMISSIONER_STOPPING: stopping commissioner...");
        
        if(!startCliCommand("commissioner stop", "Done", 3000)) {
            logLightThread(LT_LOG_WARN, "COMMISSIONER_STOPPING: CLI busy");
            return;
        }
    }

    if(cliCommandDone()){
        cliDone = false;
        logLightThread(LT_LOG_INFO, "COMMISSIONER_STOPPING: Stopped");
        setState(State::STANDBY);
        return;
    }

    if(cliCommandFailed()){
        cliFailed = false;
        logLightThread(LT_LOG_WARN, "COMMISSIONER_STOPPING: Timeout, forcing standby anyway");
        setState(State::STANDBY);
        return;
    }
}
