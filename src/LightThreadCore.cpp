#include "LightThread.h"

// Constructor: sets initial state and configures button pin
LightThread::LightThread()
    : buttonPin(BUTTON_PIN), state(State::INIT) {
    pinMode(buttonPin, INPUT_PULLUP);
}

// Begin routine: initializes CLI, resets state machine
void LightThread::begin() {
    logLightThread(LT_LOG_INFO,"LightThread begin()");
    OThreadCLI.begin(false);        // Start CLI interface (non-blocking)
    OThreadCLI.setTimeout(250);     // Set CLI read timeout
    setState(State::INIT);          // Enter INIT state
}

// Main loop update: handles input and state transitions
void LightThread::update() {
    handleButton();         // Check for button presses
    processState();         // Call the handler for current state

    String cliBuffer;
    String udpLine;
    bool isUDP;
    
    // Read characters from CLI and process full lines
    while (OThreadCLI.available()) {
        char c = OThreadCLI.read();

        if (processCLIChar(c, cliBuffer, isUDP, udpLine)) {
            if (isUDP) {
                handleUdpLine(udpLine);  // Handle incoming UDP
            } else {
                handleCliLine(cliBuffer);  // Handle CLI output
                cliBuffer = "";            // Clear buffer
            }
        }
    }

    updateLighting();       // Update RGB LED
    updateReliableUdp();    // Retry pending reliable messages

}

// Sets the current FSM state and resets its entry timer
void LightThread::setState(State newState) {
    if (state != newState) {
        logLightThread(LT_LOG_INFO,"State transition: %d → %d", static_cast<int>(state), static_cast<int>(newState));
        state = newState;
        stateEntryTime = millis();
        justEntered = true;  // <- Set on entry
    }
}

// Checks if currently in a specific FSM state
bool LightThread::inState(State expected) const {
    return state == expected;
}

// Returns how long the current state has been active
unsigned long LightThread::timeInState() const {
    return millis() - stateEntryTime;
}

// Dispatches the appropriate handler for the current state
void LightThread::processState() {
    switch (state) {
        case State::INIT:                 handleInit(); break;
        case State::STANDBY:              handleStandby(); break;

	case State::LEADER_WAIT_NETWORK:  handleLeaderWaitNetwork(); break;
        case State::COMMISSIONER_START:   handleCommissionerStart(); break;
        case State::COMMISSIONER_ACTIVE:  handleCommissionerActive(); break;

        case State::JOINER_START:         handleJoinerStart(); break;
        case State::JOINER_SCAN:          handleJoinerScan(); break;
        case State::JOINER_WAIT_BROADCAST:handleJoinerWaitBroadcast(); break;
        case State::JOINER_WAIT_ACK:      handleJoinerWaitAck(); break;
        case State::JOINER_PAIRED:        handleJoinerPaired(); break;
        case State::JOINER_RECONNECT:     handleJoinerReconnect(); break;
        case State::JOINER_SEEKING_LEADER:handleJoinerSeekingLeader();

        case State::ERROR:               handleError(); break;
        default:                         logLightThread(LT_LOG_WARN,"Unknown state"); break;
    }
}

// Initial state: load config, setup network, choose FSM path
void LightThread::handleInit() {
    if (justEntered) {
	justEntered = false;
		
	if (!loadNetworkConfig()) {
		setState(State::ERROR);
		return;
	}
		
	String tmp;
        if (role == Role::LEADER) {
            // Setup the Thread network from scratch
            logLightThread(LT_LOG_INFO,"LEADER detected. Bootstrapping network setup...");

            execAndMatch("dataset init new", "Done");
            execAndMatch("dataset channel " + String(configuredChannel), "Done");
            execAndMatch("dataset panid " + configuredPanid, "Done");
            execAndMatch("dataset networkkey 00112233445566778899aabbccddeeff", "Done");
            execAndMatch("dataset meshlocalprefix " + configuredPrefix, "Done");
            execAndMatch("dataset commit active", "Done");
            execAndMatch("ifconfig up", "Done");
            execAndMatch("thread start", "Done");

            setState(State::LEADER_WAIT_NETWORK);
        } 
        else {
	  if (loadLeaderInfo(leaderIp, tmp)) {
	    logLightThread(LT_LOG_INFO,"INIT: Joiner has saved leader info: %s", leaderIp.c_str());
	    setState(State::JOINER_RECONNECT);
	  }
	  else {
	    logLightThread(LT_LOG_INFO,"INIT: No saved leader info, standby");
	    setState(State::STANDBY);
	  }
	}
    }
}


// Leader standby: monitor joiner heartbeats and remove stale entries
void LightThread::handleStandby() {
    if (role != Role::LEADER) return;

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 5000) return;
    lastCheck = millis();

    unsigned long now = millis();
    for (auto it = joinerHeartbeatMap.begin(); it != joinerHeartbeatMap.end(); ) {
        if (now - it->second > 15000) {
            logLightThread(LT_LOG_WARN,"Joiner %s timed out — removing from heartbeat map", it->first.c_str());
            it = joinerHeartbeatMap.erase(it);
        } else {
            ++it;
        }
    }
}

// Placeholder error handler (can be expanded)
void LightThread::handleError() {}

// Reads the button and responds to short/long presses
void LightThread::handleButton() {
    static bool buttonPressed = false;
    static unsigned long pressStart = 0;

    bool isPressed = digitalRead(buttonPin) == LOW;

    if (isPressed && !buttonPressed) {
        buttonPressed = true;
        pressStart = millis();
        logLightThread(LT_LOG_INFO,"Button press started");

    } else if (!isPressed && buttonPressed) {
        buttonPressed = false;
        unsigned long duration = millis() - pressStart;

        if (duration < 50) {
            logLightThread(LT_LOG_INFO,"Ignored press (debounce)");
            return;
        }

        if (duration >= 3000) {
          // Long press = factory reset (for joiners only)
            logLightThread(LT_LOG_INFO,"Long press");
            if (role == Role::JOINER) {
                clearPersistentState();
                setState(State::STANDBY);
            }
        } 
        else {
            // Short press = trigger pairing
            logLightThread(LT_LOG_INFO,"Short press");
            if (state == State::STANDBY) {
                setState(role == Role::LEADER ? State::COMMISSIONER_START : State::JOINER_START);
            }
        }
    }
}


// Updates the onboard RGB LED color based on current FSM state
void LightThread::updateLighting() {
#ifdef RGB_BUILTIN
    static unsigned long lastBlink = 0;
    static bool ledOn = false;

    auto set = [](int r, int g, int b) {
        rgbLedWrite(RGB_BUILTIN, g, r, b); // Assume GRB
    };

    auto blink = [&](int r, int g, int b) {
        if (millis() - lastBlink > 500) {
            ledOn = !ledOn;
            lastBlink = millis();
        }
        set(ledOn ? r : 0, ledOn ? g : 0, ledOn ? b : 0);
    };

    switch (state) {
        case State::INIT:                  set(255, 165, 0); break;        // orange
        case State::STANDBY:               set(0, 0, 255); break;          // blue
		
	case State::LEADER_WAIT_NETWORK:   blink(255, 165, 0); break; // orange blink

        case State::COMMISSIONER_START:    blink(255, 60, 0); break;      // dark orange
        case State::COMMISSIONER_ACTIVE:   blink(0, 255, 0); break;        // blinking green

        case State::JOINER_START:          blink(0, 255, 255); break;      // cyan
        case State::JOINER_SCAN:           blink(135, 206, 250); break;    // light sky blue
        case State::JOINER_WAIT_BROADCAST: blink(0, 128, 255); break;      // bluish green
        case State::JOINER_WAIT_ACK:       blink(0, 128, 255); break;
        case State::JOINER_PAIRED:         set(0, 255, 0); break;          // solid green
        case State::JOINER_RECONNECT:      blink(255, 255, 0); break;      // blinking yellow
        case State::JOINER_SEEKING_LEADER: blink(255,60,0); break;        //blinking orange

        case State::ERROR:                 blink(255, 0, 0); break;        // blinking red
        default:                           set(255, 0, 255); break;        // magenta (unknown)
    }
#endif
}
