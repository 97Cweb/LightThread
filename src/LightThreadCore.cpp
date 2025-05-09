#include "LightThread.h"

#include "esp_mac.h"
#include "mbedtls/md.h"  // For hashing (part of ESP32 libraries)
#include "nvs_flash.h"
#include "nvs.h"

LightThread::LightThread(uint8_t buttonPin, Role role, const String& leaderIp, const String& networkKey, const String& networkName, int32_t channel, 
                         uint16_t udpPort, const String& joinerKey, const String& panid)
    : buttonPin(buttonPin), role(role), leaderIp(leaderIp), networkKey(networkKey), networkName(networkName), channel(channel), 
      udpPort(udpPort), joinerKey(joinerKey), panid(panid), currentState(INITIALIZING) {
    
	// Initialize last received acknowledgment
    memset(lastReceivedAck, 0, sizeof(lastReceivedAck));
    lastReceivedAckLength = 0;

    // Configure the button pin as an input with pull-up
    pinMode(buttonPin, INPUT_PULLUP);

    // Initialize NVS (non-volatile storage)
    esp_err_t nvsResult = nvs_flash_init();
    if (nvsResult == ESP_ERR_NVS_NO_FREE_PAGES || nvsResult == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    log_i("LightThread constructor complete.");
}




void LightThread::begin() {
	log_i("Starting LightThread...");
    setState(INITIALIZING);
	
	
    OThreadCLI.begin(false);     // No AutoStart is necessary
    OThreadCLI.setTimeout(250);  // Waits 250ms for the OpenThread CLI response
    // Log the unique device hash
    log_d("Device hash: %s", hashMacAddress());

    log_i("LightThread started.");
}

void LightThread::update() {
	handleButton();
	
    switch (currentState) {
        case INITIALIZING:
            handleInitializing();
            break;
        case STANDBY:
			handleStandby();
            break;
        case COMMISSIONER_START:
            handleCommissionerStart();
            break;
        case COMMISSIONER_ACTIVE:
            handleCommissionerActive();
            break;
        case JOINER_START:
            handleJoinerStart();
            break;
		case JOINER_GET_NETWORK:
			handleJoinerNetwork();
			break;
		case JOINER_RECONNECT:
            handleJoinerReconnect();
            break;
        case JOINER_LISTEN_FOR_BROADCAST:
            handleJoinerListenForBroadcast();
            break;
		case JOINER_WAIT_FOR_ACK:
			handleJoinerWaitForAck();
        case JOINER_PAIRED:
            handleJoinerPaired();
            break;
		case ERROR:
			break;
        default:
            log_w("Warning: Unknown State.");
            break;
    }

    // Update lighting regardless of the current state
    updateLighting();
}

void LightThread::handleButton(){
	
	// Check if the button is currently pressed
    if (digitalRead(buttonPin) == LOW) {
        if (!buttonStartFlag) {
            // Button just pressed
            buttonStartFlag = true;
            buttonPressStart = millis(); // Record the press start time
            log_d("Button press started.");
        }else{
			 // Check for long press duration
            unsigned long pressDuration = millis() - buttonPressStart;
            if (pressDuration >= LONG_PRESS_THRESHOLD_MS) {
                // Perform double blink during long press
                log_i("Long button press detected, performing double blink.");
                for (int i = 0; i < 2; ++i) { // Two blinks
                    setLightColor(255, 255, 0); // Yellow (or desired color)
                    delay(200);                 // Light on for 200ms
                    setLightColor(0, 0, 0);     // Light off
                    delay(200);                 // Light off for 200ms
                }
                log_i("Double blink pattern completed. Waiting for button release.");
				setState(STANDBY);
                return; // Ensure the double blink only happens once
            }
		}
    } else if (buttonStartFlag) {
        // Button was pressed and now released
        unsigned long pressDuration = millis() - buttonPressStart;
        buttonStartFlag = false; // Reset the flag

        // Debounce check
        if (pressDuration < DEBOUNCE_DELAY_MS) {
            log_d("Button press ignored due to debounce.");
            return;
        }

        // Handle long or short press
        if (pressDuration < LONG_PRESS_THRESHOLD_MS) {
            // Handle short press
            log_i("Short button press detected.");
            if (currentState != STANDBY) {
                return;
            }
            timerStart = millis();
            timeoutStart = millis();
            if (role == LEADER) {
                setState(COMMISSIONER_START);
                log_i("Commissioner mode enabled.");
            } else if (role == JOINER) {
                setState(JOINER_START);
                log_i("Joiner mode enabled.");
            }
        }else {
            log_i("Button released after long press.");
            if (role == JOINER) {
                clearNetworkInfo(); // Clear stored network information
                log_i("Network info cleared.");
				setState(STANDBY);
            }
		}
    }
}

bool LightThread::initializeRole() {
    if (role == LEADER) {
        return initializeLeader();
    } else if (role == JOINER) {
        return initializeJoiner();
    }
    log_e("Unknown role. Initialization failed.");
    return false;
}
	
bool LightThread::initializeLeader() {
    log_i("Initializing as Leader...");

    // Initialize dataset
    if (!otExecCommand("dataset init new", "", &returnCode) ||
        String(returnCode.errorMessage).indexOf("Done") == -1) {
        log_e("Failed to initialize dataset.");
        return false;
    }

    // Set network configurations
    if (!setDataset("dataset channel", String(channel)) ||
        !setDataset("dataset panid", panid) ||
        !setDataset("dataset networkname", networkName) ||
        !setDataset("dataset networkkey", networkKey) ||
		!setDataset("dataset meshlocalprefix", getMeshLocalPrefix())) {
        return false;
    }

    // Commit dataset and start network
    if (!commitDataset() || !bringUpInterface() || !startThread() || !openUDPSocket()) {
        return false;
    }
	

    // Ensure device becomes leader
    while (otGetDeviceRole() != OT_ROLE_LEADER) {
        log_d("Waiting to become leader...");
        delay(IS_LEADER_SCAN_DELAY_MS);
    }

    log_i("Leader initialization complete.");
    return true;
}

bool LightThread::initializeJoiner() {
    log_i("Initializing as Joiner...");

    // Load network information or use defaults
    if (loadInfo()) {
        log_i("Information loaded from memory.");
        if (!setDataset("dataset networkkey", networkKey) ||
            !setDataset("dataset panid", panid) ||
            !setDataset("dataset channel", String(channel))) {
            return false;
        }
    } else {
        log_i("No network information found. Using constructor's settings.");
        if (!otExecCommand("dataset clear", "", &returnCode) ||
            String(returnCode.errorMessage).indexOf("Done") == -1 ||
			!setDataset("dataset networkkey", networkKey) ||
            !setDataset("dataset panid", PAN_ID_SCAN_ALL) ||
			!setDataset("dataset channel", String(channel))) {
            return false;
        }
    }
	
	

    // Commit dataset and start network
    if (!commitDataset() || !bringUpInterface() || !startThread() || !openUDPSocket()) {
        return false;
    }

    log_i("Joiner initialization complete.");
    return true;
}

bool LightThread::setDataset(const String& command, const String& value) {
    if (!value.isEmpty()) {
        if (!otExecCommand((command + " " + value).c_str(), "", &returnCode) ||
            String(returnCode.errorMessage).indexOf("Done") == -1) {
            log_e("Failed to set %s with value: %s", command.c_str(), value.c_str());
            return false;
        }
        log_i("%s set to %s", command.c_str(), value.c_str());
    } else {
        log_w("Value for %s is empty.", command.c_str());
        return false;
    }
    return true;
}

bool LightThread::commitDataset() {
    return otExecCommand("dataset commit active", "", &returnCode) &&
           String(returnCode.errorMessage).indexOf("Done") != -1;
}

bool LightThread::bringUpInterface() {
    return otExecCommand("ifconfig up", "", &returnCode) &&
           String(returnCode.errorMessage).indexOf("Done") != -1;
}

bool LightThread::startThread(){
	// Start Thread network
    otExecCommand("thread start", "", &returnCode);

    if (String(returnCode.errorMessage).indexOf("Done") != -1 || String(returnCode.errorMessage).indexOf("Idle") != -1) {
        log_i("Joiner successfully started thread");
    } else {
        return false;
    }
	return true;
}

bool LightThread::stopThread(){
    
	
	// Stop Thread network
    if (!otExecCommand("thread stop", "", &returnCode)) {
        log_e("Failed to stop thread.");
        return false;
    }
	
	return true;
}

void LightThread::setState(LightThreadState newState) {
	if(newState == STANDBY && role == JOINER){
		stopThread();
	}
	
    log_d("State transition: %d -> %d", currentState, newState);
    currentState = newState;
	timerStart = millis(); // Reset timer for the new state
    updateLighting();
}

void LightThread::handleInitializing() {
    log_i("Handling initialization for role: %s", role == LEADER ? "Leader" : "Joiner");

    if(initializeRole()) {
        setState(STANDBY); // Transition to standby if successful
    } else {
        setState(ERROR); // Transition to error state on failure
    }
}

void LightThread::handleStandby() {
    if(role == LEADER){ //only leaders can receive and send packets in standby, as standby is waiting to join for joiners
		sendHeartbeats();
		handleUdpActive();
	}else if (role == JOINER){
		/*
		if(!leaderIp.isEmpty()){
			startThread();
			setState(JOINER_RECONNECT);
		}
		*/
	}
}




bool LightThread::otGetResp(char* resp, size_t respSize, bool& isUDP) {

    static char buffer[RESPONSE_BUFFER_SIZE];       // Internal static buffer to accumulate messages
    static size_t bufferIndex = 0; // Current position in the buffer
	
    while (OThreadCLI.available() > 0) {
        char c = OThreadCLI.read();
        if (bufferIndex < sizeof(buffer) - 1) { // Prevent overflow
            buffer[bufferIndex++] = c;
        }

        // Check for end of a line (message delimiter)
        if (c == '\n') {
            buffer[bufferIndex] = '\0'; // Null-terminate the message

            log_d("Received CLI message: %s", buffer);

            // Copy to response if requested
            if (resp != nullptr && respSize > 0) {
                strncpy(resp, buffer, respSize - 1);
                resp[respSize - 1] = '\0'; // Ensure null termination
            }
            // Set the isUDP flag based on the content format
            if (strstr(buffer, "from ") != nullptr) {
                isUDP = true; // Indicates the message is a UDP packet
            } else {
                isUDP = false; // Indicates the message is a CLI response
            }
			
            bufferIndex = 0; // Reset buffer for next message
            return true;     // Indicate a message was received
        }
    }
    return false; // No message received
}

bool LightThread::waitForString(String& responseBuffer, unsigned long timeoutMs, const String& thisString) {
    responseBuffer = ""; // Clear the buffer
    char response[RESPONSE_BUFFER_SIZE] = {0};
    unsigned long startTime = millis();

    while (millis() - startTime < timeoutMs) {
        bool isUDP = false;
        if (otGetResp(response, sizeof(response), isUDP)) {
            responseBuffer += String(response); // Accumulate response data

            if (String(response).indexOf(thisString) != -1) {
                return true; // Successfully received "Done"
            }
        }

        delay(50); // Avoid tight looping
    }

    log_w("Timeout while waiting for 'Done'.");
    return false; // Timed out without receiving "Done"
}

void LightThread::printMessage(const uint8_t* message, size_t messageLength) {
    if (message == nullptr || messageLength == 0) {
        return;
    }

    Serial.print("Message content (length ");
    Serial.print(messageLength);
    Serial.println("):");

    for (size_t i = 0; i < messageLength; ++i) {
        Serial.print("0x");
        if (message[i] < 0x10) Serial.print("0"); // Add leading zero for single digit
        Serial.print(message[i], HEX);
        Serial.print(" ");
    }
    Serial.println(); // Add a newline for clarity
}

String LightThread::convertToHexString(const uint8_t* data, size_t length) {
    String hexString;
    for (size_t i = 0; i < length; ++i) {
        char hexByte[3]; // Two hex digits + null terminator
        snprintf(hexByte, sizeof(hexByte), "%02X", data[i]); // Format as two-character hex
        hexString += String(hexByte);
    }
    return hexString;
}

bool LightThread::convertHexStringToBinary(const char* hexString, uint8_t* binary, size_t& binaryLength) {
    if (hexString == nullptr) {
        log_e("Error: Null hex string provided.");
        return false;
    }

    size_t hexLength = strlen(hexString);
    if (hexLength % 2 != 0) {
        log_e("Error: Invalid hex string length.");
        return false;
    }

    binaryLength = hexLength / 2;
    for (size_t i = 0; i < binaryLength; ++i) {
        char byteString[3] = {hexString[i * 2], hexString[i * 2 + 1], '\0'}; // Extract two characters
        binary[i] = (uint8_t)strtol(byteString, nullptr, 16); // Convert hex to binary
    }

    return true;
}

void LightThread::updateLighting() {
    switch (currentState) {
        case INITIALIZING:
            setLightColor(255, 165, 0); // Orange for initializing (warm busy)
            break;
        case STANDBY:
            setLightColor(0, 0, 255); // Blue for standby (cool ready)
            break;
        case COMMISSIONER_START:
            setLightBlink(255, 140, 0); // Dark orange blinking for starting (warm busy)
            break;
        case COMMISSIONER_ACTIVE:
            setLightBlink(0, 255, 0); // green blinking for active commissioner (cool busy)
            break;
        case JOINER_START:
            setLightBlink(0, 255, 255); // Cyan blinking for joiner starting (cool busy)
            break;
        case JOINER_GET_NETWORK:
            setLightBlink(135, 206, 250); // Light sky blue blinking for getting network (cool busy)
            break;
		case JOINER_RECONNECT:
            setLightBlink(135, 206, 250);  // yellow blinking for trying to rejoin (cool busy)
            break;
        case JOINER_LISTEN_FOR_BROADCAST:
            setLightBlink(0, 128, 255); // green blinking for listening (cool busy)
            break;
		case JOINER_WAIT_FOR_ACK:
			setLightBlink(0,128,255);
			break;
        case JOINER_PAIRED:
            setLightColor(0, 255, 0); // Green for paired (cool ready)
            break;
        case ERROR:
            setLightBlink(255, 0, 0); // Blinking red for errors
            break;
        default:
            setLightColor(255, 0, 255); // missing texture pink for undefined states
            break;
    }
}

void LightThread::setLightColor(int red, int green, int blue) {
#ifdef RGB_BUILTIN
    rgbLedWrite(RGB_BUILTIN, green, red, blue);
#endif
}

void LightThread::setLightBlink(int red, int green, int blue) {
    static unsigned long lastBlinkTime = 0;
    static bool lightOn = false;

    if (millis() - lastBlinkTime > BLINK_TOGGLE_MS) {
        lightOn = !lightOn;
        lastBlinkTime = millis();
    }

    if (lightOn) {
        setLightColor(red, green, blue);
    } else {
        setLightColor(0, 0, 0);
    }
}

const char*  LightThread::hashMacAddress() const {
	static char hashedId[65];  // 64 characters for SHA-256 hash + null terminator
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);  // Get the MAC address

    uint8_t hash[32];  // For SHA-256
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, mac, sizeof(mac));
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    // Convert hash to a C-string
    for (int i = 0; i < 32; i++) {
        snprintf(&hashedId[i * 2], 3, "%02X", hash[i]);
    }
    hashedId[64] = '\0';  // Null-terminate the string

    return hashedId;
}

