#include "LightThread.h"

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
  if(justEntered){
    justEntered = false;

    logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_WAIT_NETWORK - Waiting for a thread attachment");
  }

  if(isThreadAttached()){
    refreshMyIp();

    if(!openUdp()){
      setState(State::ERROR);
    }

    logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_WAIT_NETWORK - Attached as %s", thread.otGetStringDeviceRole());

    setState(State::JOINER_DISCOVER_LEADER);
    return;
  }

  if(timeInState() >= LIGHTTHREAD_ATTACH_TIMEOUT_MS){
    logLightThread(LIGHTTHREAD_LOG_ERROR, "JOINER_WAIT_NETWORK - Attachment timed out - role = %s", thread.otGetStringDeviceRole());
    setState(State::ERROR);
  }


}

void LightThread::handleJoinerDiscoverLeader() {
  if(justEntered){
    justEntered = false;

    logLightThread(LIGHTTHREAD_LOG_INFO, "JOINER_DISCOVER_LEADER - Waiting for pairing broadcast");
  }

  //udpcomm.cpp handles the discovery exchange. 
  //PAIRING_BROADCAST received, send pairing request
  //PAIRING_RESPONSE received, remember leaderIP, enter JOINER_PAIRED

  if(!isThreadAttached()){
    logLightThread(LIGHTTHREAD_LOG_WARN, "JOINER_DISCOVER_LEADER - Thread attachment lost");

    closeUdp();
    setState(State::JOINER_RECONNECT);
    return;
  }

  if(timeInState() >= LIGHTTHREAD_JOINER_TIMEOUT_MS){
    logLightThread(LIGHTTHREAD_LOG_WARN, "JOINER_DISCOVER_LEADER - Pairing broadcast timed out");

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
  static unsigned long lastDiscoveryRequest = 0;
  
  if(justEntered){
    justEntered = false;

    leaderIp = IPAddress();
    lastDiscoveryRequest = 0;

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

  if(!udpOpen){
    refreshMyIp();

    if(!openUdp()){
      setState(State::ERROR);
      return;
    }
  }
  
  //get leader address

  if(lastDiscoveryRequest == 0 ||
       millis() - lastDiscoveryRequest >=
           LIGHTTHREAD_LEADER_BROADCAST_INTERVAL_MS) {

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
    setState(State::ERROR);
}
