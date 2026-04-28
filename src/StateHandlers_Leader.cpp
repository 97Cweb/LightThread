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
        logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER_WAIT_NETWORK: Waiting for Thread network...");
    }

    if(timeInState() > LIGHTTHREAD_LEADER_TIMEOUT_MS){
        logLightThread(LIGHTTHREAD_LOG_ERROR, "LEADER_WAIT_NETWORK: Timed out waiting for leader state");
        setState(State::ERROR);
        return;
    }

    if(cliCommandFailed()){
        logLightThread(LIGHTTHREAD_LOG_WARN, "LEADER_WAIT_NETWORK: CLI command failed");
        cliFailed = false;
        cliDone = false;
        cliBusy = false;
        step = QUERY_STATE;
        return;
    }

    switch(step){
        case QUERY_STATE:
            if(timeInState()-lastCheck < LIGHTTHREAD_CLI_STATE_CHECK_INTERVAL_MS) return;
            lastCheck = timeInState();
            if(startCliCommand("state","",LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS)){
                step = WAIT_STATE_RESPONSE;
            }
            break;

        case WAIT_STATE_RESPONSE: {
            if(!cliCommandDone()) return;

            String response = getCliResponse();
            cliDone = false;
            if(response.indexOf("leader") != -1 || response.indexOf("router") != -1){
                logLightThread(LIGHTTHREAD_LOG_INFO,
                                "LEADER_WAIT_NETWORK: Thread is up in state: %s",
                                response.c_str());
                startCliCommand("udp open", LIGHTTHREAD_CLI_DONE, LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);
                step = UDP_OPEN;
            }
            else{
                logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER_WAIT_NETWORK: Not leader/router yet");
                step = QUERY_STATE;
            }
            break;
        }
        case UDP_OPEN:
            if(!cliCommandDone()) return;
            
            cliDone = false;
            startCliCommand(String("udp bind :: ") + LIGHTTHREAD_UDP_PORT, LIGHTTHREAD_CLI_DONE,LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);

            step = UDP_BIND;
            break;
        case UDP_BIND:
            if(!cliCommandDone()) return;

            cliDone = false;
            startCliCommand(LIGHTTHREAD_CLI_MLEID_COMMAND, "",LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);
            step = GET_MLEID;
            break;

        case GET_MLEID:
            if(!cliCommandDone()) return;
            
            cliDone = false;
            captureMyIpFromResponse(getCliResponse());

            logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER_WAIT_NETWORK: UDP ready");
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
        logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_START: starting commissioner...");

        if(!startCliCommand("commissioner start", LIGHTTHREAD_CLI_DONE, LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS)){
            logLightThread(LIGHTTHREAD_LOG_WARN, "COMMISSIONER_START: CLI busy");
            return;
        }
    }
    
    if(cliCommandDone()){
        cliDone = false;
        logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_START: commissioner started");
        setState(State::COMMISSIONER_ACTIVE);
        return;
    }
    
    if(cliCommandFailed()){
        cliFailed = false;
        logLightThread(LIGHTTHREAD_LOG_ERROR, "COMMISSIONER_START: Failed to start commissioner");
        setState(State::ERROR);
        return;
    }
}

// Sends pairing broadcasts periodically while in commissioner active mode.
// Transitions to STANDBY after 60 seconds.
void LightThread::handleCommissionerActive() {
    static unsigned long lastBroadcast = 0;

    // Broadcast PAIRING signal
    if(millis() - lastBroadcast > LIGHTTHREAD_LEADER_BROADCAST_INTERVAL_MS) {
        lastBroadcast = millis();

        std::vector<uint8_t> emptyPayload;
        bool ok = sendUdpPacket(MessageType::PAIRING_BROADCAST, emptyPayload,
                                LIGHTTHREAD_MULTICAST_ALL_NODES, // multicast all nodes
                                LIGHTTHREAD_UDP_PORT);

        if(ok) {
            logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_ACTIVE: Sent PAIR_REQUEST broadcast");
        } else {
            logLightThread(LIGHTTHREAD_LOG_WARN, "COMMISSIONER_ACTIVE: Failed to send PAIR_REQUEST");
        }
    }

    // End commissioning after 60 seconds
    if(timeInState() > LIGHTTHREAD_LEADER_TIMEOUT_MS) {
        logLightThread(LIGHTTHREAD_LOG_INFO,
                   "COMMISSIONER_ACTIVE: Pairing timed out. Stopping commissioner");
        setState(State::COMMISSIONER_STOPPING);
        return;
    }
}

void LightThread::handleCommissionerStopping(){
    if(justEntered){
        justEntered = false;
        logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_STOPPING: stopping commissioner...");
        
        if(!startCliCommand("commissioner stop", LIGHTTHREAD_CLI_DONE, LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS)) {
            logLightThread(LIGHTTHREAD_LOG_WARN, "COMMISSIONER_STOPPING: CLI busy");
            return;
        }
    }

    if(cliCommandDone()){
        cliDone = false;
        logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_STOPPING: Stopped");
        setState(State::STANDBY);
        return;
    }

    if(cliCommandFailed()){
        cliFailed = false;
        logLightThread(LIGHTTHREAD_LOG_WARN, "COMMISSIONER_STOPPING: Timeout, forcing standby anyway");
        setState(State::STANDBY);
        return;
    }
}
