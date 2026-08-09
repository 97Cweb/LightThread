#include "LightThread.h"
#include "LightThreadTypes.h"
#include "openthread/error.h"

#include <openthread/instance.h>
#include <openthread/link.h>
#include <openthread/thread.h>

#include <esp_openthread_lock.h>

void LightThread::handleJoinerStart() {
  if(!justEntered){
    return;  
  }
  justEntered = false;
  
  if(thread.hasActiveDataset()){
    logLightThread(LIGHTTHREAD_LOG_WARN, "JOINER_START - Active dataset already exists");
    setState(State::JOINER_RECONNECT);
    return;
  }    

  logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_START - Preparing Thread Joiner");

  //restrict discovery to configured network, channel and panid are hints
  thread.setChannel(configuredChannel);
  thread.setPanId(configuredPanId);
  thread.setExtendedPanId(LIGHTTHREAD_EXTENDED_PAN_ID);

  //IPV6 enabled, thread not started
  thread.networkInterfaceUp();

  logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_START - Starting commissioning on channel %u", configuredChannel);

  otError error = thread.startJoiner(LIGHTTHREAD_JOINER_PSKD, LIGHTTHREAD_JOINER_START_TIMEOUT_MS);

  if(error != OT_ERROR_NONE){
    logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER_START - Commissioning failed: %s (%d)", otThreadErrorToString(error), static_cast<int>(error));

    thread.networkInterfaceDown();
    setState(State::STANDBY);
    return;  
  }
  logLightThread(LIGHTTHREAD_LOG_INFO,"JOINER_START - Commissioning succeeded - dataset received");


  thread.start();
  setState(State::JOINER_WAIT_NETWORK);
}

void LightThread::handleJoinerWaitNetwork() {
    if(isThreadAttached()){
        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_WAIT_NETWORK - Attached as %s", thread.otGetStringDeviceRole());


        setState(State::JOINER_APPLY_POWER_MODE);
        return;
    }
    if(timeInState() >= LIGHTTHREAD_ATTACH_TIMEOUT_MS){
        logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER_WAIT_NETWORK - Attachment timed out - role = %s", thread.otGetStringDeviceRole());
        setState(State::ERROR);
    }
}

void LightThread::handleJoinerApplyPowerMode() {
    if(justEntered) {
        justEntered = false;

        switch(configuredPowerMode) {
            case PowerMode::AWAKE:
                logLightThread(
                    LIGHTTHREAD_LOG_INFO,
                    "JOINER_APPLY_POWER_MODE - Configuring awake router-eligible mode"
                );
                break;

            case PowerMode::SLEEPY:
                logLightThread(
                    LIGHTTHREAD_LOG_INFO,
                    "JOINER_APPLY_POWER_MODE - Switching attached child to sleepy mode"
                );
                break;

            case PowerMode::DORMANT:
                logLightThread(
                    LIGHTTHREAD_LOG_INFO,
                    "JOINER_APPLY_POWER_MODE - Switching attached child to dormant operating mode"
                );
                break;
        }

        if(!configureJoinerPowerMode()) {
            setState(State::ERROR);
            return;
        }

        if(configuredPowerMode == PowerMode::AWAKE) {
            logLightThread(
                LIGHTTHREAD_LOG_INFO,
                "JOINER_APPLY_POWER_MODE - Awake mode applied; Thread connection retained"
            );
        } else {
            logLightThread(
                LIGHTTHREAD_LOG_INFO,
                "JOINER_APPLY_POWER_MODE - Power mode applied; waiting for Thread child reattachment"
            );
        }
    }

    if(!isThreadAttached()) {
        if(timeInState() >= LIGHTTHREAD_ATTACH_TIMEOUT_MS) {
            logLightThread(
                LIGHTTHREAD_LOG_ERROR,
                "JOINER_APPLY_POWER_MODE - Reattachment timed out - role=%s",
                thread.otGetStringDeviceRole()
            );

            setState(State::ERROR);
        }

        return;
    }

    logLightThread(
        LIGHTTHREAD_LOG_INFO,
        "JOINER_APPLY_POWER_MODE - Thread attached in final mode as %s",
        thread.otGetStringDeviceRole()
    );

    refreshMyIp();

    if(!openUdp()) {
        setState(State::ERROR);
        return;
    }

    setState(State::JOINER_DISCOVER_LEADER);
}

void LightThread::handleJoinerDiscoverLeader() {
    static unsigned long lastPairingRequest = 0;

    if(justEntered) {
        justEntered = false;
        lastPairingRequest = 0;

        logLightThread(
            LIGHTTHREAD_LOG_INFO,
            "JOINER_DISCOVER_LEADER - Searching for commissioning leader"
        );
    }

    if(!isThreadAttached()) {
        logLightThread(
            LIGHTTHREAD_LOG_WARN,
            "JOINER_DISCOVER_LEADER - Thread attachment lost"
        );

        closeUdp();
        setState(State::JOINER_RECONNECT);
        return;
    }

    if(
        lastPairingRequest == 0 ||
        millis() - lastPairingRequest >=
            LIGHTTHREAD_LEADER_BROADCAST_INTERVAL_MS
    ) {
        IPAddress multicastAddress;

        if(!multicastAddress.fromString("ff03::1")) {
            logLightThread(
                LIGHTTHREAD_LOG_ERROR,
                "JOINER_DISCOVER_LEADER - Invalid multicast address"
            );

            setState(State::ERROR);
            return;
        }

        std::vector<uint8_t> identity =
            hashToBytes(generateMacHash());

        if(sendUdpPacket(
            MessageType::PAIRING_REQUEST,
            identity,
            multicastAddress,
            LIGHTTHREAD_UDP_PORT
        )) {
            logLightThread(
                LIGHTTHREAD_LOG_INFO,
                "JOINER_DISCOVER_LEADER - Pairing request sent"
            );
        }

        lastPairingRequest = millis();
    }

    if(timeInState() >= LIGHTTHREAD_JOINER_TIMEOUT_MS) {
        logLightThread(
            LIGHTTHREAD_LOG_WARN,
            "JOINER_DISCOVER_LEADER - Pairing request timed out"
        );

        closeUdp();
        setState(State::JOINER_RECONNECT);
    }
}
void LightThread::handleJoinerPaired() {
  if(justEntered){
    justEntered = false;

    logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_PAIRED - Leader is %s", leaderIp.toString().c_str());  
  }

  if(!isThreadAttached()){
    logLightThread(LIGHTTHREAD_LOG_WARN, "JOINER_PAIRED - Thread attachment lost");

    leaderIp = IPAddress();
    closeUdp();

    setState(State::JOINER_RECONNECT);
    return;
  }
}

void LightThread::handleJoinerReconnect() {
    static bool powerModeApplied = false;
    static unsigned long lastDiscoveryRequest = 0;
  
    if(justEntered){
        justEntered = false;

        leaderIp = IPAddress();
        lastDiscoveryRequest = 0;
        powerModeApplied = false;

        logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_RECONNECT - Resuming stored Thread network");

        closeUdp();
    
        if(!thread.hasActiveDataset()){
            logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER_RECONNECT - No stored thread dataset");

            setState(State::STANDBY);
            return;
        }

        thread.networkInterfaceUp();
        

        thread.start();
    }

    if(!isThreadAttached()){
        if(timeInState() >= LIGHTTHREAD_ATTACH_TIMEOUT_MS){
            logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER_RECONNECT - Still waiting for thread attachment - role = %s",thread.otGetStringDeviceRole());
            stateEntryTime = millis();
      
        }
    return;
    }

    if(!powerModeApplied){
        if(!configureJoinerPowerMode()){
            setState(State::ERROR);
            return;
        }
        powerModeApplied = true;
        if(!isThreadAttached()){
            return;
        }
    }

    if(!udpOpen){
        refreshMyIp();
        if(!openUdp()){
            setState(State::ERROR);
            return;
        }
    }
  
  //get leader address

    if(lastDiscoveryRequest == 0 || millis() - lastDiscoveryRequest >=    LIGHTTHREAD_LEADER_BROADCAST_INTERVAL_MS) {

        IPAddress multicastAddress;

        if(!multicastAddress.fromString("ff03::1")) {
            logLightThread(
                LIGHTTHREAD_LOG_ERROR,
                "JOINER_RECONNECT - Invalid multicast address"
            );

            setState(State::ERROR);
            return;
        }

        std::vector<uint8_t> identity =
            hashToBytes(generateMacHash());

        if(sendUdpPacket(
            MessageType::DISCOVERY_REQUEST,
            identity,
            multicastAddress,
            LIGHTTHREAD_UDP_PORT
        )) {
            logLightThread(
                LIGHTTHREAD_LOG_INFO,
                "JOINER_RECONNECT - Leader discovery request sent"
            );
        }

        lastDiscoveryRequest = millis();
    }

    if(timeInState() >= LIGHTTHREAD_JOINER_TIMEOUT_MS) {
        logLightThread(
            LIGHTTHREAD_LOG_ERROR,
            "JOINER_RECONNECT - Leader discovery timed out"
        );

        setState(State::ERROR);
    }

}

void LightThread::handleJoinerFactoryReset() {
  if(!justEntered){
    return;
  }
  justEntered = false;
  
  logLightThread(LIGHTTHREAD_LOG_WARN, "JOINER_FACTORY_RESET - Erasing Thread credentials");

  closeUdp();
  clearPersistentState();
  thread.stop();
  thread.networkInterfaceDown();

  otInstance *instance = thread.getInstance();

  if(instance == nullptr){
    logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER_FACTORY_RESET - OpenThread instance unavailable");

    setState(State::ERROR);
    return;
  }

  if(!esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))){
    logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER_FACTORY_RESET - Could not acquire OpenThread lock");

    setState(State::ERROR);
    return;
  }
  
  logLightThread(LIGHTTHREAD_LOG_WARN, "JOINER_FACTORY_RESET - Factory reset now");

  otInstanceFactoryReset(instance);

  esp_openthread_lock_release();
  
  logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER_FACTORY_RESET - Platform restart did not occur");
  setState(State::ERROR);
}




bool LightThread::configureJoinerPowerMode(){
    otInstance *instance = thread.getInstance();

    if(instance == nullptr){
        logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER - OpenThread instance unavailable");
        return false;
    }

    if(!esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))){
        logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER - Could not acquire OpenThread lock");
        return false;
    }

    otLinkModeConfig linkMode{};

    switch(configuredPowerMode){
        case PowerMode::AWAKE:
            linkMode.mRxOnWhenIdle = true;
            linkMode.mDeviceType = true;
            linkMode.mNetworkData = true;
            break;

        case PowerMode::SLEEPY:
        case PowerMode::DORMANT:
            linkMode.mRxOnWhenIdle = false;
            linkMode.mDeviceType = false;
            linkMode.mNetworkData = false;
            break;
    }

    otError modeError = otThreadSetLinkMode(instance, linkMode);

    otError pollError = OT_ERROR_NONE;

    if(modeError == OT_ERROR_NONE){
        switch(configuredPowerMode){
            case PowerMode::AWAKE:
                pollError = otLinkSetPollPeriod(instance,0);
                break;

            case PowerMode::SLEEPY:
            case PowerMode::DORMANT:
                pollError = otLinkSetPollPeriod(instance,configuredPollIntervalMs);

                otThreadSetChildTimeout(instance,configuredChildTimeoutSec);
                break;
        }
    }

    esp_openthread_lock_release();

    if(modeError != OT_ERROR_NONE) {
        logLightThread(
            LIGHTTHREAD_LOG_ERROR,
            "JOINER - Could not set Thread link mode: %s",
            otThreadErrorToString(modeError)
        );

        return false;
    }

    if(pollError != OT_ERROR_NONE){
        logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER - Could not set poll period: %s", otThreadErrorToString(pollError));
        return false;
    }

    switch(configuredPowerMode){
        case PowerMode::AWAKE:
            logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER - Power mode awake, receiver always on");
            break;

        case PowerMode::SLEEPY:
            logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER - Power mode sleepy, poll=%lums timeout %lus", static_cast<unsigned long>(configuredPollIntervalMs), static_cast<unsigned long>(configuredChildTimeoutSec));
            break;
        case PowerMode::DORMANT:
            logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER - Power mode dormant, poll=%lums wake=%lus",static_cast<unsigned long>(configuredPollIntervalMs),static_cast<unsigned long>(configuredDormantWakeAfterSeconds));
            break;
    }

    return true;
}
