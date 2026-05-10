#include "LightThread.h"


void LightThread::handleLeaderInit(){

    const LightThread::CliStep leaderSetupCommands[] = {
            {"dataset init new",                                        ""},
            {String("dataset channel ") + configuredChannel,            ""},
            {String("dataset panid ") + configuredPanid,                ""},
            {String("dataset networkkey ") + LIGHTTHREAD_NETWORK_KEY,   ""},
            {String("dataset meshlocalprefix ") + configuredPrefix,     ""},
            {"dataset commit active",                                   ""},
            {"ifconfig up",                                             ""},
            {"thread start",                                            ""},
        };
    if(justEntered){
        justEntered = false;
        resetCliSteps();
    }

    if(!runCliSteps(leaderSetupCommands,8)){
        return;
    }


    setState(State::LEADER_WAIT_NETWORK);
}

// Waits for the Thread network to come up and become a leader or router.
// Once stable, binds the UDP socket and transitions to STANDBY.
void LightThread::handleLeaderWaitNetwork() {
    const LightThread::CliStep leaderWaitNetworkSteps[] = {
            { "state",                                          "leader|router" },
            { "udp open", "" },
            { String("udp bind :: ") + LIGHTTHREAD_UDP_PORT,    "" },
            { "ipaddr mleid",                                   "" }
        };    

    if(justEntered){
        justEntered = false;
        resetCliSteps();
    }

    if(timeInState() > LIGHTTHREAD_LEADER_TIMEOUT_MS) {
        logLightThread(LIGHTTHREAD_LOG_ERROR,
                       "LEADER_WAIT_NETWORK: Timed out waiting for leader state");
        setState(State::ERROR);
        return;
    }

    if(!runCliSteps(leaderWaitNetworkSteps, 4)) {
        return;
    }

    captureMyIpFromResponse(getCliResponse());

    logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER_WAIT_NETWORK: UDP ready");
    setState(State::STANDBY);
 
}

// Begins the commissioner role and adds a wildcard joiner filter.
void LightThread::handleCommissionerStart() {
    const LightThread::CliStep leaderCommissionerStartCommands[] = {
            {"commissioner start",                                        ""}
        };

    if(justEntered) {
        justEntered = false;
        resetCliSteps();
    }

    if(!runCliSteps(leaderCommissionerStartCommands, 1)) {
        return;
    }
    
    setState(State::COMMISSIONER_ACTIVE);
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

    const LightThread::CliStep leaderCommissionerStopCommands[] = {
                {"commissioner stop",                                        ""}
        };
    if(justEntered){
        justEntered = false;
        resetCliSteps();

        logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_STOPPING: stopping commissioner...");
        
    }

    if(!runCliSteps(leaderCommissionerStopCommands, 1)) {
        return;
    }

    setState(State::STANDBY);
}
            
