#include "LightThread.h"

// Constructor: sets initial state and configures button pin
LightThread::LightThread() : buttonPin(LIGHTTHREAD_DEFAULT_BUTTON_PIN), state(State::INIT) {
    pinMode(LIGHTTHREAD_DEFAULT_BUTTON_PIN, INPUT_PULLUP);
}

// Begin routine: initializes CLI, resets state machine
void LightThread::begin() {
    logLightThread(LIGHTTHREAD_LOG_INFO, "LightThread begin()");
    OThread.begin(false);    // Start CLI interface (non-blocking)
    OThreadCLI.begin();
    OThreadCLI.setTimeout(LIGHTTHREAD_CLI_SERIAL_TIMEOUT_MS); // Set CLI read timeout
    setState(State::INIT);      // Enter INIT state
}

// Main loop update: handles input and state transitions
void LightThread::update() {
    handleButton(); // Check for button presses
    
    readCliSerial();
    processState(); // Call the handler for current state
    updateCliCommand();
    updateLighting();    // Update RGB LED
}

// Sets the current FSM state and resets its entry timer
void LightThread::setState(State newState) {
    if(state != newState) {
        logLightThread(LIGHTTHREAD_LOG_INFO, "State transition: %d → %d", static_cast<int>(state),
                       static_cast<int>(newState));
        state = newState;
        stateEntryTime = millis();
        justEntered = true; // <- Set on entry
    }
}

// Checks if currently in a specific FSM state
bool LightThread::inState(State expected) const { return state == expected; }

// Returns how long the current state has been active
unsigned long LightThread::timeInState() const { return millis() - stateEntryTime; }

// Dispatches the appropriate handler for the current state
void LightThread::processState() {
    switch(state) {
    case State::INIT:
        handleInit();
        break;
    case State::STANDBY:
        handleStandby();
        break;

    case State::LEADER_WAIT_NETWORK:
        handleLeaderWaitNetwork();
        break;
    case State::COMMISSIONER_START:
        handleCommissionerStart();
        break;
    case State::COMMISSIONER_ACTIVE:
        handleCommissionerActive();
        break;
    case State::COMMISSIONER_STOPPING:
        handleCommissionerStopping();
        break;
    case State::JOINER_START:
        handleJoinerStart();
        break;
    case State::JOINER_SCAN:
        handleJoinerScan();
        break;
    case State::JOINER_WAIT_BROADCAST:
        handleJoinerWaitBroadcast();
        break;
    case State::JOINER_WAIT_RESPONSE:
        handleJoinerWaitAck();
        break;
    case State::JOINER_PAIRED:
        handleJoinerPaired();
        break;
    case State::JOINER_RECONNECT:
        handleJoinerReconnect();
        break;
    case State::JOINER_SEEKING_LEADER:
        handleJoinerSeekingLeader();
        break;
    case State::JOINER_FACTORY_RESET:
        handleJoinerFactoryReset();
        break;

    case State::ERROR:
        handleError();
        break;
    default:
        logLightThread(LIGHTTHREAD_LOG_WARN, "Unknown state");
        break;
    }
}

// Initial state: load config, setup network, choose FSM path
void LightThread::handleInit() {
    static int leaderInitStep = 0;
    if(justEntered) {
        justEntered = false;
        leaderInitStep = 0;

        if(!loadNetworkConfig()) {
            setState(State::ERROR);
            return;
        }

        String tmp;

        if(role != Role::LEADER){
            if(loadLeaderInfo(leaderIp, tmp)) {
                logLightThread(LIGHTTHREAD_LOG_INFO, "INIT: Joiner has saved leader info: %s",
                               leaderIp.c_str());
                setState(State::JOINER_RECONNECT);
            } 
            else {
                logLightThread(LIGHTTHREAD_LOG_INFO, "INIT: No saved leader info, standby");
                setState(State::STANDBY);
            }
            return;
        }
        logLightThread(LIGHTTHREAD_LOG_INFO, "LEADER detected. Bootstrapping network setup...");
    }

    if (role != Role::LEADER) return;

    const String commands[] = {
        "dataset init new",
        "dataset channel " + String(configuredChannel),
        "dataset panid " + configuredPanid,
        String("dataset networkkey ") + LIGHTTHREAD_NETWORK_KEY,
        "dataset meshlocalprefix " + configuredPrefix,
        "dataset commit active",
        "ifconfig up",
        "thread start"
    };

    const int commandCount = sizeof(commands) / sizeof(commands[0]);

    if (cliCommandFailed()) {
        cliFailed = false;
        logLightThread(LIGHTTHREAD_LOG_ERROR, "INIT: CLI command failed");
        setState(State::ERROR);
        return;
    }

    if (cliCommandDone()) {
        cliDone = false;
        leaderInitStep++;

        if (leaderInitStep >= commandCount) {
            setState(State::LEADER_WAIT_NETWORK);
            return;
        }

        startCliCommand(commands[leaderInitStep], "Done", LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);
        return;
    }

    if (!cliBusy) {
        startCliCommand(commands[leaderInitStep], "Done", LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);
    }

}

// Leader standby: monitor joiner heartbeats and remove stale entries
void LightThread::handleStandby() {
    if(role != Role::LEADER)
        return;

    static unsigned long lastCheck = 0;
    if(millis() - lastCheck < LIGHTTHREAD_CLI_STATE_CHECK_INTERVAL_MS)
        return;
    lastCheck = millis();

    unsigned long now = millis();
    for(auto it = joinerHeartbeatMap.begin(); it != joinerHeartbeatMap.end();) {
        if(now - it->second > LIGHTTHREAD_HEARTBEAT_TIMEOUT_MS) {
            logLightThread(LIGHTTHREAD_LOG_WARN, "Joiner %s timed out — removing from heartbeat map",
                           it->first.c_str());
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

    if(isPressed && !buttonPressed) {
        buttonPressed = true;
        pressStart = millis();
        logLightThread(LIGHTTHREAD_LOG_INFO, "Button press started");

    } else if(!isPressed && buttonPressed) {
        buttonPressed = false;
        unsigned long duration = millis() - pressStart;

        if(duration < LIGHTTHREAD_BUTTON_DEBOUNCE_MS) {
            logLightThread(LIGHTTHREAD_LOG_INFO, "Ignored press (debounce)");
            return;
        }

        if(duration >= LIGHTTHREAD_BUTTON_LONG_PRESS_MS) {
            // Long press = factory reset (for joiners only)
            logLightThread(LIGHTTHREAD_LOG_INFO, "Long press");
            if(role == Role::JOINER) {
                setState(State::JOINER_FACTORY_RESET);
            }
        } else {
            // Short press = trigger pairing
            logLightThread(LIGHTTHREAD_LOG_INFO, "Short press");
            if(state == State::STANDBY) {
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
        rgbLedWrite(RGB_BUILTIN, g, r, b); // Assume GRB, TODO: set to RGB when using new boards
    };

    auto blink = [&](int r, int g, int b) {
        if(millis() - lastBlink > LIGHTTHREAD_LED_BLINK_INTERVAL_MS) {
            ledOn = !ledOn;
            lastBlink = millis();
        }
        set(ledOn ? r : 0, ledOn ? g : 0, ledOn ? b : 0);
    };

    switch(state) {
    case State::INIT:
        set(255, 165, 0);
        break; // orange
    case State::STANDBY:
        set(0, 0, 255);
        break; // blue

    case State::LEADER_WAIT_NETWORK:
        blink(255, 165, 0);
        break; // orange blink

    case State::COMMISSIONER_START:
        blink(255, 60, 0);
        break; // dark orange
    case State::COMMISSIONER_ACTIVE:
        blink(0, 255, 0);
        break; // blinking green

    case State::JOINER_START:
        blink(0, 255, 255);
        break; // cyan
    case State::JOINER_SCAN:
        blink(135, 206, 250);
        break; // light sky blue
    case State::JOINER_WAIT_BROADCAST:
        blink(0, 128, 255);
        break; // bluish green
    case State::JOINER_WAIT_RESPONSE:
        blink(0, 128, 255);
        break;
    case State::JOINER_PAIRED:
        set(0, 255, 0);
        break; // solid green
    case State::JOINER_RECONNECT:
        blink(255, 255, 0);
        break; // blinking yellow
    case State::JOINER_SEEKING_LEADER:
        blink(255, 60, 0);
        break; // blinking orange

    case State::ERROR:
        blink(255, 0, 0);
        break; // blinking red
    default:
        set(255, 0, 255);
        break; // magenta (unknown)
    }
#endif
}
