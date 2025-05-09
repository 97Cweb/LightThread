#include "LightThread.h"


bool LightThread::startJoin() {
    log_i("Attempting to start joiner...");
    // Send the joiner start command
    otExecCommand("joiner start", joinerKey.c_str(), &returnCode);
    String errorMsg = String(returnCode.errorMessage);

    if (errorMsg.indexOf("Done") != -1) {
        log_i("Joiner start command accepted. Monitoring state...");
        return true;
    }

    log_w("Failed to start joiner due to: %s", errorMsg.c_str());

    return false;
}

bool LightThread::checkJoinerState() {
	OThreadCLI.println("joiner state");
	String joinerState = "";
	if (!waitForString(joinerState, NETWORK_TIMEOUT_MS, "Done")) { // 5-second timeout
        log_w("Timeout while retrieving network information.");
        return false;
    }
	

    log_d("Joiner state response: %s", joinerState.c_str());


    if (joinerState.indexOf("success") != -1 || joinerState.indexOf("Idle") != -1) {
		log_i("Joiner successfully connected or idle.");
	} else {
		return false;
	}
	
	if(startThread()){
		log_i("Successfully started thread");
	}else{
		return false;
	}
    return true;
}


void LightThread::handleJoinerStart() {
    static bool joinAttempted = false;

    if (!joinAttempted) {
        if (startJoin()) {
            joinAttempted = true;
			timeoutStart = millis();
        } else {
            log_w("Failed to start joiner. Returning to standby.");
            setState(STANDBY);
            return;
        }
    }
	if(millis() - timeoutStart > RETRY_TIMEOUT_MS){
		log_w("Joiner timeout. Returning to standby from joiner start");
		setState(STANDBY);
		joinAttempted = false;
	}
    // Monitor joiner state
    if (millis() - timerStart > RETRY_INTERVAL_MS) { // Retry every second
        if (checkJoinerState()) {
            setState(JOINER_GET_NETWORK);
            joinAttempted = false;
        }
		timerStart = millis();
    }
}


void LightThread::handleJoinerNetwork(){
	if (millis() - timerStart > RETRY_INTERVAL_MS) { // Retry every 2 seconds
        if (getNetworkInfo()) {
            setState(JOINER_LISTEN_FOR_BROADCAST);
			timeoutStart = millis();
			timerStart = millis(); // Reset timer
        }
        timerStart = millis(); // Reset timer
    }
	
	// Timeout handling for joiner
    if (millis() - timeoutStart > RETRY_TIMEOUT_MS) { // 20 second timeout
        log_w("Joiner timeout. Returning to standby from listen.");
        setState(STANDBY);
    }
}

void LightThread::handleJoinerListenForBroadcast() { //broadcast detection
    handleUdpActive();
	
	// Timeout handling for joiner
    if (millis() - timeoutStart > RETRY_TIMEOUT_MS) { // 20 second timeout
        log_w("Joiner timeout. Returning to standby from listen.");
        setState(STANDBY);
    }
}

void LightThread::handleJoinerWaitForAck(){
	handleUdpActive();
	if(millis()-timeoutStart > RETRY_TIMEOUT_MS){
		log_w("PAIR_ACK not received, returning to standby from wait for ack");
		setState(STANDBY);
	}
}




void LightThread::handleJoinerReconnect() {
    static bool udpSent = false;

    if (!udpSent) {
		if(otGetDeviceRole() == OT_ROLE_CHILD || otGetDeviceRole() == OT_ROLE_ROUTER){
			// Send "I'm back" UDP packet to the leader
			const char* hashedMac = hashMacAddress();
			uint8_t message[65]; // Assuming hashMacAddress() produces a 64-character string + null terminator
			strncpy((char*)message, hashedMac, sizeof(message));

			sendUdpPacket(message, sizeof(message), leaderIp, udpPort, ACK_REQUIRED, RECONNECT_NOTIFY);  // Leader IP should be known
			log_i("Reconnection notification sent to leader.");
			udpSent = true;
			timeoutStart = millis();  // Start the timeout timer
		}else{
			log_d("current role: %s", otGetStringDeviceRole());
		}
		
        
    }

    handleUdpActive();

    // Timeout handling
    if (millis() - timeoutStart > REJOIN_TIMEOUT_MS) {
		log_w("Could not reconnect. Returning to STANDBY.");
		setState(STANDBY);
		udpSent = false;  // Reset flag
		timeoutStart = millis();
    }
}





void LightThread::handleJoinerPaired(){
	sendHeartbeats();
	handleUdpActive();
	
}




bool LightThread::getNetworkInfo() {
    log_i("Retrieving network information...");
    String datasetInfo = "";  // Accumulate dataset information

    // Send the dataset active command once
    OThreadCLI.println("dataset active");

    if (!waitForString(datasetInfo, NETWORK_TIMEOUT_MS, "Done")) { // 5-second timeout
        log_w("Timeout while retrieving network information.");
        return false;
    }
	Serial.println(datasetInfo);

    // Parse and log dataset fields
    if (datasetInfo.indexOf("Network Key:") != -1) {
        int startIndex = datasetInfo.indexOf("Network Key:") + NETWORK_KEY_OFFSET;
        int endIndex = datasetInfo.indexOf('\n', startIndex);
        networkKey = datasetInfo.substring(startIndex, endIndex);	
		networkKey.trim();
        log_d("Network Key: %s", networkKey.c_str());

    }

    if (datasetInfo.indexOf("Channel:") != -1) {
        int startIndex = datasetInfo.indexOf("Channel:") + CHANNEL_OFFSET;
        int endIndex = datasetInfo.indexOf('\n', startIndex);
        String channelStr = datasetInfo.substring(startIndex, endIndex);
		channelStr.trim();
		channel = channelStr.toInt(); // Convert the string to an integer
        log_d("Channel: %s", channelStr.c_str());

    }

    if (datasetInfo.indexOf("PAN ID:") != -1) {
        int startIndex = datasetInfo.indexOf("PAN ID:") + PAN_ID_OFFSET;
        int endIndex = datasetInfo.indexOf('\n', startIndex);
        panid = datasetInfo.substring(startIndex, endIndex);
		panid.trim();
        log_d("PAN ID: %s", panid.c_str());

    }

    // Save the extracted network info
    if (!networkKey.isEmpty() && !panid.isEmpty() && channel > 0) {
        if (saveNetworkInfo()) {
            log_i("Network information saved successfully.");
        } else {
            log_e("Failed to save network information.");
            return false;
        }
    } else {
        log_w("Incomplete network information, not saved.");
        return false;
    }

    return true;
}



