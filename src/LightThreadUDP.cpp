#include "LightThread.h"

// Open UDP socket
bool LightThread::openUDPSocket(){
	if(role == LEADER){
		otExecCommand("ipaddr add ", leaderIp.c_str(), &returnCode);
	}
	
	otExecCommand("udp open", "", &returnCode);
	String errorMsg =  String(returnCode.errorMessage);
    if (errorMsg.indexOf("Already") != -1){
		log_i("udp already opened");
	}else if(errorMsg.indexOf("Done") == -1) {
        log_w("Failed to open UDP socket.");
        return false;
    }
	
	// Bind UDP socket
	String udpBindCommand = "udp bind ";
	if(role == LEADER){
		udpBindCommand += leaderIp + " ";
	}else{
		udpBindCommand += ":: ";
	}
	udpBindCommand += String(udpPort);
	
	if (!otExecCommand(udpBindCommand.c_str(), "", &returnCode) || String(returnCode.errorMessage).indexOf("Done") == -1) {
		log_w("Failed to bind UDP socket.");
		return false;
	}
	return true;
}

void LightThread::checkPendingAcks() {
	log_v("Pending Acks: %zu packets", pendingPackets.size());

    auto now = millis();
    for (auto it = pendingPackets.begin(); it != pendingPackets.end();) {
        if (now - it->timestamp > ACK_RETRY_TIMEOUT_MS) {
            // Resend the packet if timeout expired
			const uint8_t* packetData = reinterpret_cast<const uint8_t*>(it->data.c_str());
            if (!sendUdpPacket(packetData, it->data.length(), it->destIp, it->destPort, ACK_REQUIRED_RETRY, it->messageType)) {
                log_w("Failed to resend packet to %s:%u", it->destIp.c_str(), it->destPort);
            } else {
                log_d("Resent packet to %s:%u", it->destIp.c_str(), it->destPort);
            }
            it->timestamp = now; // Reset the timestamp
        }

        if (receivedAckFor(it->data)) {
            it = pendingPackets.erase(it); // Remove from queue if acknowledged
        } else {
            ++it;
        }
    }
}

bool LightThread::receivedAckFor(const String& data) {
    // Check if the last received acknowledgment matches the first byte and content of the packet
    if (lastReceivedAckLength == 0 || data.isEmpty()) {
        return false; // No acknowledgment received or invalid packet
    }
	// Convert the last acknowledgment to a String for comparison
    String lastAckData(reinterpret_cast<const char*>(lastReceivedAck), lastReceivedAckLength);
    
    // Compare the strings directly
    return data == lastAckData;
}




bool LightThread::sendUdpPacket(const uint8_t* data, size_t length, const String& destIp, uint16_t destPort, AckType ackType, MessageType messageType) {
    
	// Validate inputs
	if(destIp.isEmpty() || destPort == 0){
		log_w("No destination, cannot send UDP packet. AckType: %u  MessageType: %u",ackType, messageType);
		return false;
	}
    if (data == nullptr && length == 0 ) {
		if(ackType == NO_ACK && messageType == NORMAL){			
			log_w("Invalid arguments to sendUdpPacket.");
			return false;
		}
    }
	
	// Construct the packet with the command byte prepended
    uint8_t packetData[RESPONSE_BUFFER_SIZE]; // Adjust size as needed
    packetData[0] = static_cast<uint8_t>(ackType);
	packetData[1] = static_cast<uint8_t>(messageType);
    memcpy(packetData + 2, data, length);

	String args = destIp + " " + String(destPort) + " ";	
	args += convertToHexString(packetData, 2 + length );
	
	log_d("Sending: %s", args.c_str());

	otExecCommand("udp send", args.c_str(), &returnCode);
    if (String(returnCode.errorMessage).indexOf("Done") == -1) {
        log_w("Failed to send UDP packet: %s", returnCode.errorMessage.c_str());
        return false;
    }

    log_i("UDP packet sent.");
	
	if (ackType == ACK_REQUIRED) {
        // Store the packet in a queue with a timeout for acknowledgment
		// Convert packet data to String and store in PacketInfo
		String packetDataStr;
		for (size_t i = 0; i < length + 2; ++i) {
			packetDataStr += static_cast<char>(packetData[i]);
		}
        PacketInfo packet = {packetDataStr, destIp, destPort, millis(), messageType};
        pendingPackets.push_back(packet);
    }
	
    return true;
}

bool LightThread::parseResponseAsUDP(const char* response, String& srcIp, uint16_t& srcPort, uint8_t* message, size_t& messageLength, MessageType& messageType) {
    if (!response || !*response) {
        log_w("Empty or invalid response.");
        return false;
    }
	
	
	// Parse the message length from the response
    const char* lengthStart = response;
    const char* lengthEnd = strchr(lengthStart, ' ');
    if (!lengthEnd) {
        log_w("Message length not found in response.");
        return false;
    }
    int reportedLength = atoi(String(lengthStart, lengthEnd - lengthStart).c_str());
	
	
    if (reportedLength <= 0 || reportedLength > RESPONSE_BUFFER_SIZE) {
        log_w("Invalid message length: %d", reportedLength);
        return false;
    }

    // Parse source IP
    const char* ipStart = strstr(response, "from ");
    if (!ipStart) {
        log_w("Source IP not found in response.");
        return false;
    }
    ipStart += 5; // Move past "from "
    const char* ipEnd = strchr(ipStart, ' ');
    if (!ipEnd) {
        log_w("Invalid source IP format.");
        return false;
    }
    srcIp = String(ipStart, ipEnd - ipStart);

    // Parse source port
    const char* portStart = ipEnd + 1;
    const char* portEnd = strchr(portStart, ' ');
    if (!portEnd) {
        log_w("Source port not found in response.");
        return false;
    }
    srcPort = atoi(String(portStart, portEnd - portStart).c_str());

    // Parse payload
    const char* dataStart = portEnd + 1;


    if (reportedLength == 0 || reportedLength > RESPONSE_BUFFER_SIZE *2 || reportedLength % 2 != 0) {
        log_w("Invalid hex payload length.");
        return false;
    }

    // Convert hex string to binary
    size_t binaryLength = reportedLength / 2;
    for (size_t i = 0; i < binaryLength; ++i) {
        char byteString[3] = {dataStart[i * 2], dataStart[i * 2 + 1], '\0'}; // Extract two characters
        message[i] = (uint8_t)strtol(byteString, nullptr, 16); // Convert hex to binary
    }
	
    messageLength = binaryLength;
	
	// Extract and handle the ackType
    if (messageLength < 2) {
        log_w("Invalid message: insufficient length for ackType and messageType.");
        return false;
    }
	
	int bytesToRemove = 1 + 1;//exclude command byte, message byte

    // Extract and handle the ackType
    uint8_t ackType = message[0]; // Extract the ackType byte
	messageType = static_cast<MessageType>(message[1]); // Extract the command byte
    size_t actualMessageLength = messageLength - bytesToRemove;
    memmove(message, message + 2, actualMessageLength); // Shift message content left
    messageLength = actualMessageLength;
	
	if (ackType == ACK_REQUIRED || ackType == ACK_REQUIRED_RETRY) { // ACK_REQUIRED
        // Send acknowledgment packet
        if (!sendUdpPacket(message, messageLength, srcIp, srcPort, RETURN_ACK, messageType)) {
            log_e("Failed to send acknowledgment.");
            return false;
        }
    } else if (ackType == RETURN_ACK) {
		memcpy(lastReceivedAck, message, messageLength);
		lastReceivedAckLength = messageLength;
		log_d("Not propagating RETURN_ACK message from %s:%u", srcIp.c_str(), srcPort);
		
		// Clear the buffer to prevent reuse of stale data
		memset(message, 0, messageLength);
		messageLength = 0;
		return true; // Return early without propagating
	}

	
    log_d("Parsed Packet Details:\n  Source IP: %s\n  Source Port: %u\n  ackType: %u\n  messageType: %u\n  Payload (binary):", srcIp.c_str(), srcPort, ackType, messageType);
	String payloadStr;
	for (size_t i = 0; i < messageLength; ++i) {
		if (message[i] < 0x10) payloadStr += "0";
		payloadStr += String(message[i], HEX) + " ";
	}
	log_d("%s", payloadStr.c_str());


    return true;
}

void LightThread::handleUdpActive() {
	handleResponses();
    checkPendingAcks();
}

void LightThread::sendHeartbeats(){
	
	unsigned long now = millis();
	if (now - timerStart > HEARTBEAT_INTERVAL_MS){
		if(role == LEADER){
			for (int i = 0; i < joinerCount; ++i){
				if(joinerTable[i].timer.hasLapsed()){
					log_w("Joiner timed out: %s", joinerTable[i].uniqueId.c_str());
					
					// Remove the joiner's pending packets
                    auto it = pendingPackets.begin();
                    while (it != pendingPackets.end()) {
                        if (it->destIp == joinerTable[i].ip && it->destPort == joinerTable[i].port) {
                            log_d("Removing packet destined for: %s:%u", it->destIp.c_str(), it->destPort);
                            it = pendingPackets.erase(it);
                        } else {
                            ++it;
                        }
                    }

                    // Remove the joiner from the joinerTable
                    log_d("Removing joiner: %s", joinerTable[i].uniqueId.c_str());
                    for (int j = i; j < joinerCount - 1; ++j) {
                        joinerTable[j] = joinerTable[j + 1]; // Shift left
                    }
                    joinerCount--; // Reduce the count
                    i--; // Adjust the index after removal
				}else{
					sendUdpPacket(nullptr,0,joinerTable[i].ip, joinerTable[i].port, ACK_REQUIRED, HEARTBEAT);
				}
			}
			
		}else{
			if(leaderHeartbeatTimer.hasLapsed()){
				log_w("Leader timed out");
				setState(JOINER_RECONNECT);
			}else{
				sendUdpPacket(nullptr,0,leaderIp,udpPort, ACK_REQUIRED, HEARTBEAT);
			}
		}
		timerStart = now;
	}

}

void LightThread::handleResponses(){
	char response[RESPONSE_BUFFER_SIZE];
    bool isUDP = false;
    if (otGetResp(response, sizeof(response), isUDP) && isUDP) {
        String srcIp;
        uint16_t srcPort;
        uint8_t message[RESPONSE_BUFFER_SIZE];
        size_t messageLength = 0;
		MessageType messageType;
		log_d("response: %s", response);

        if (parseResponseAsUDP(response, srcIp, srcPort, message, messageLength,messageType)) {
			
            switch (messageType) {
				case NORMAL:
					if (normalPacketHandler) {
						normalPacketHandler(message, messageLength, srcIp);
					}
					break;

				case PAIR_REQUEST:
                    if (currentState == JOINER_LISTEN_FOR_BROADCAST) {
						if(leaderIp.isEmpty()){
							// Joiner received PAIR_REQUEST, responds with its hashed MAC
							const char* hashedMac = hashMacAddress();
							uint8_t hashedMacMessage[65];
							strncpy((char*)hashedMacMessage, hashedMac, sizeof(hashedMacMessage));
							
							if (sendUdpPacket(hashedMacMessage, sizeof(hashedMacMessage), srcIp, srcPort, ACK_REQUIRED, PAIR_REQUEST)) {
								log_i("PAIR_REQUEST response sent to leader: %s", srcIp.c_str());
								leaderIp = srcIp;
								saveLeaderIp();
								setState(JOINER_WAIT_FOR_ACK);
							} else {
								log_w("Failed to send PAIR_REQUEST to leader: %s", srcIp.c_str());
							}
						}
                    } else if (currentState == COMMISSIONER_ACTIVE) {
                        // Leader received a PAIR_REQUEST from joiner
                        saveJoiner(srcIp, srcPort, message, messageLength);
						
                        if (sendUdpPacket(message, messageLength, srcIp, srcPort, ACK_REQUIRED, PAIR_ACK)) {
                            log_i("PAIR_ACK sent to joiner: %s", srcIp.c_str());
                        } else {
                            log_w("Failed to send PAIR_ACK to joiner: %s", srcIp.c_str());
                        }
						delay(50);
						setState(STANDBY);
                    }
                    break;
					
				case PAIR_ACK:
                    if (currentState == JOINER_WAIT_FOR_ACK) {
                        // Joiner received PAIR_ACK from leader, handshake complete
                        log_i("Joiner paired successfully with leader: %s", srcIp.c_str());
						leaderHeartbeatTimer.reset();
                        setState(JOINER_PAIRED);
                    } else if (currentState == JOINER_RECONNECT) {
                        // Joiner received PAIR_ACK from leader, handshake complete
                        log_i("Joiner re-paired successfully with leader: %s", srcIp.c_str());
                        setState(JOINER_PAIRED);
                    }
                    break;
					
                case RECONNECT_NOTIFY: //leader notified of joiner back
					if(currentState == COMMISSIONER_ACTIVE || currentState == STANDBY){						
						if (sendUdpPacket(message, messageLength, srcIp, srcPort, ACK_REQUIRED, PAIR_ACK)) {
							log_i("PAIR_ACK response sent to joiner: %s", srcIp.c_str());
							
							reconnectJoiner(srcIp, srcPort, message, messageLength);
							log_i("Joiner reconnected: %s", srcIp.c_str());
						} else {
							log_w("Failed to send PAIR_ACK to joiner: %s", srcIp.c_str());
						}
					}					
                    break;
					
				case HEARTBEAT:
					if(role == LEADER){
						log_i("Heartbeat received from joiner: %s", srcIp.c_str());
						bool joinerFound = false;
						log_d("Joiner Count: %u", joinerCount);
						for (int i = 0; i<joinerCount; ++i){
							if(joinerTable[i].ip.equals(srcIp) && joinerTable[i].port == srcPort){
								joinerTable[i].timer.reset();
								log_d("Heartbeat timer reset for joiner: %s", joinerTable[i].uniqueId.c_str());
								joinerFound = true;
								break;
							}
						}
						if (!joinerFound) {
							log_w("Received heartbeat from unknown joiner: %s", srcIp.c_str());
						}
						
					} else{
						log_i("Heartbeat received from leader");
                        // Reset joiner's heartbeat timer
                        leaderHeartbeatTimer.reset();
					}
					break;
                default:
                    log_w("Unknown UDP message type received.");
                    break;
            }
        } else {
            log_w("Failed to parse UDP response.");
        }
    }
}

String LightThread::getMeshLocalPrefix() {
    // Find the first four colon-separated groups
    int count = 0;
    int pos = 0;

    // Iterate through the string to locate the fourth colon
    for (size_t i = 0; i < leaderIp.length(); i++) {
        if (leaderIp.charAt(i) == ':') {
            count++;
            if (count == 4) {
                pos = i;
                break;
            }
        }
    }

    // Extract the prefix up to the fourth colon and append "::"
    String meshLocalPrefix = leaderIp.substring(0, pos) + "::";
    return meshLocalPrefix;
}



String LightThread::findUniqueIdByIp(const String& ip, uint16_t port) const {
    for (int i = 0; i < joinerCount; i++) {
        if (joinerTable[i].ip == ip && joinerTable[i].port == port) {
            return joinerTable[i].uniqueId;
        }
    }
    return "";
}
