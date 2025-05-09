#include "LightThread.h"


void LightThread::handleCommissionerStart() {
    if (millis() - timerStart > RETRY_INTERVAL_MS) { // Retry every 2 seconds
        if (startCommissioner()) {
            setState(COMMISSIONER_ACTIVE);
			timeoutStart = millis();
        }
        timerStart = millis(); // Reset timer
		
    }
}

void LightThread::handleCommissionerActive() {
    if (millis() - timerStart > COMMISSIONING_BROADCAST_RATE_MS) { // Broadcast every 3 seconds
        sendUdpPacket(nullptr, 0, MULTICAST_IP, udpPort, NO_ACK,PAIR_REQUEST);
        timerStart = millis();
        log_d("Leader broadcast sent.");
    }
    // Timeout handling for commissioner
    if (millis() - timeoutStart > COMMISSIONING_TIMEOUT_MS) { // 1-minute timeout
        log_i("Commissioner timeout. Returning to standby.");
        stopCommissioner();
        setState(STANDBY);
    }
	
	handleUdpActive();
}



void LightThread::reconnectJoiner(const String& ip, uint16_t port, const uint8_t* hash, size_t hashLength) {
    // Convert the binary hash to a hexadecimal string
    String hashStr = String((const char*)hash);
    log_d("Reconnect hash: %s", hashStr);

    // Search for the joiner in the table by unique ID
    for (int i = 0; i < joinerCount; i++) {
        if (joinerTable[i].uniqueId.equalsIgnoreCase(hashStr)) {  // Case-insensitive match
            joinerTable[i].ip = ip;
            joinerTable[i].port = port;
			joinerTable[i].timer.reset();
            log_i("Joiner reconnected: %s", ip.c_str());
            return;
        }
    }

    log_w("Joiner not found for hash: %s", hashStr.c_str());
}




void LightThread::saveJoiner(const String& ip, uint16_t port, const uint8_t* hash, size_t hashLength) {
    if (joinerCount >= maxJoiners) {
        log_e("Cannot save joiner: Maximum capacity reached.");
        return;
    }
	


    // Convert the binary data back to a readable hex string for logging
    //String uniqueId = convertToHexString(binaryId, binaryLength);
	
	joinerTable[joinerCount].ip = ip;
	joinerTable[joinerCount].port = port;
	joinerTable[joinerCount].uniqueId = String((const char*)hash);
	joinerTable[joinerCount].timer.reset();
	joinerCount++;


    log_i("Joiner saved: IP = %s, Port = %u, Unique ID = %s", ip.c_str(), port, joinerTable[joinerCount - 1].uniqueId.c_str());
}



bool LightThread::startCommissioner() {
	OThreadCLI.println("commissioner start");
	String commissionerStartResponse = "";
	if (!waitForString(commissionerStartResponse, NETWORK_TIMEOUT_MS, "Commissioner: active")) { // 5-second timeout
        log_w("Timeout while starting commissioner");
        return false;
    }

    // Add the joiner
	if (!addJoiner()) {
		log_i("Failed to add joiner after starting commissioner.");
		return false;
	}
	log_i("Commissioner fully initialized and joiner added.");
	return true;


}

bool LightThread::addJoiner() {
	otExecCommand("commissioner joiner add * ", joinerKey.c_str(), &returnCode);
	String errorMsg = String(returnCode.errorMessage);
	if (errorMsg.indexOf("Done") == -1) {
        log_w("Failed to add joiner connection.");
        return false;
    }

    log_i("Commissioner joiner mode Active.");
    return true;
}

void LightThread::clearJoiners() {
    for (int i = 0; i < joinerCount; ++i) {
        joinerTable[i].ip = "";
        joinerTable[i].port = 0;
        joinerTable[i].uniqueId = "";
        joinerTable[i].timer.reset();

    }
    joinerCount = 0;
}




void LightThread::stopCommissioner() {
	otExecCommand("commissioner stop", "", &returnCode);
	String errorMsg =  String(returnCode.errorMessage);
    if (errorMsg.indexOf("disabled") != -1){
		log_i("commissioner stopped already");
	}else if(errorMsg.indexOf("Done") == -1) {
        log_w("Failed to stop commissioner.");
        return;
    }

    log_i("Commissioner stopped successfully.");
}


