#include "LightThread.h"

void LightThread::handleLeaderInit() {
  if(!justEntered){
    return;
  }
  justEntered = false;

  //if active dataset exists, reuse to allow reconnect easily

  if(thread.hasActiveDataset()){
    logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER_INIT: Active dataset found - resuming existing network");
  }
  else{
    logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER_INIT: No active dataset - creating network");

    dataset.initNew();
    dataset.setNetworkName(LIGHTTHREAD_NETWORK_NAME);
    dataset.setChannel(configuredChannel);
    dataset.setPanId(configuredPanId);
    dataset.setExtendedPanId(LIGHTTHREAD_EXTENDED_PAN_ID);
    dataset.setNetworkKey(LIGHTTHREAD_NETWORK_KEY);

    thread.commitDataSet(dataset);

    logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER_INIT: Dataset committed");
  }

  thread.networkInterfaceUp();
  thread.start();

  logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER_INIT: Thread started - waiting for attachment");

  setState(State::LEADER_WAIT_NETWORK);
}

void LightThread::handleLeaderWaitNetwork() {
  if(justEntered){
    justEntered = false;
    
    logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER_WAIT_NETWORK - waiting for Thread role");
  }

  ot_device_role_t threadRole = thread.otGetDeviceRole();

  if(threadRole >= OT_ROLE_CHILD){
    refreshMyIp();

    if(!openUdp()){
      setState(State::ERROR);
      return;
    }

    logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER_WAIT_NETWORK - attached as %s", thread.otGetStringDeviceRole());

    setState(State::STANDBY);
    return;
  }

  if(timeInState() > LIGHTTHREAD_LEADER_TIMEOUT_MS){
    logLightThread(LIGHTTHREAD_LOG_ERROR, "LEADER_WAIT_NETWORK: Attachment timed out; role=%s", thread.otGetStringDeviceRole());

    setState(State::ERROR);
  }
}

void LightThread::handleCommissionerStart() {
  if(!justEntered){
    return;
  }
  justEntered = false;

  if(!isThreadAttached()){
    logLightThread(LIGHTTHREAD_LOG_ERROR, "COMMISSIONER_START - Thread is not attached");
    setState(State::ERROR);
    return;  
  }

  logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_START - Petitioning for Commissioner role");

  otError error = thread.startCommissioner(LIGHTTHREAD_COMMISSIONER_START_TIMEOUT_MS);

  if(error != OT_ERROR_NONE){
    logLightThread(LIGHTTHREAD_LOG_ERROR, "COMMISIONER START - Petition failed: %s (%d)", otThreadErrorToString(error), static_cast<int>(error));

  setState(State::STANDBY);
  return;
  }

  logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_START - Commissioner active");

  error = thread.addJoiner(LIGHTTHREAD_JOINER_PSKD, LIGHTTHREAD_JOINER_WINDOW_SECONDS);

  if(error != OT_ERROR_NONE){
    logLightThread(LIGHTTHREAD_LOG_ERROR, "COMMISIONER_START - Could not authorize Joiner: %s (%d)", otThreadErrorToString(error), static_cast<int>(error));

    thread.stopCommissioner();
    setState(State::STANDBY);
    return;
  }

  logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_START: Joiner window open for %lu seconds", static_cast<unsigned long>(LIGHTTHREAD_JOINER_WINDOW_SECONDS));

  setState(State::COMMISSIONER_ACTIVE);

}

void LightThread::handleCommissionerActive() {
  if(justEntered){
    justEntered = false;


    logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_ACTIVE - Waiting for pairing request");
  }

  if(thread.getCommissionerState() != OT_COMMISSIONER_STATE_ACTIVE){
    logLightThread(LIGHTTHREAD_LOG_ERROR, "COMMISSIONER_ACTIVE - Commissioner is no longer active");

    setState(State::COMMISSIONER_STOPPING);
    return;
  }

  if(timeInState() >=LIGHTTHREAD_JOINER_WINDOW_SECONDS * 1000UL) {

    logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_ACTIVE: Joiner window expired");

    setState(State::COMMISSIONER_STOPPING);
  }
}

void LightThread::handleCommissionerStopping() {
  if(!justEntered) {
    return;
  }

  justEntered = false;

  logLightThread(LIGHTTHREAD_LOG_INFO, "COMMISSIONER_STOPPING: Stopping Commissioner");

  thread.stopCommissioner();

  setState(State::STANDBY);
}
