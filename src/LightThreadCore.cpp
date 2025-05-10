// LightThreadCore.cpp — fresh rewrite scaffold

#include "LightThread.h"

LightThread::LightThread(uint8_t buttonPin, Role role)
    : buttonPin(buttonPin), role(role), state(State::INIT) {
    pinMode(buttonPin, INPUT_PULLUP);
}

void LightThread::begin() {
    log_i("LightThread begin()");
    OThreadCLI.begin(false);
    OThreadCLI.setTimeout(250);
	setState(State::INIT);
}

void LightThread::update() {
    handleButton();
    processState();

    String cliBuffer;
    String udpLine;
    bool isUDP;

    while (OThreadCLI.available()) {
        char c = OThreadCLI.read();

        if (processCLIChar(c, cliBuffer, isUDP, udpLine)) {
            if (isUDP) {
                handleUdpLine(udpLine);
            } else {
                handleCliLine(cliBuffer);
                cliBuffer = "";
            }
        }
    }

    updateLighting();
}

void LightThread::setState(State newState) {
    if (state != newState) {
        log_i("State transition: %d → %d", static_cast<int>(state), static_cast<int>(newState));
        state = newState;
        stateEntryTime = millis();
        justEntered = true;  // <- Set on entry
    }
}

bool LightThread::inState(State expected) const {
    return state == expected;
}

unsigned long LightThread::timeInState() const {
    return millis() - stateEntryTime;
}

void LightThread::processState() {
    switch (state) {
        case State::INIT:                handleInit(); break;
        case State::STANDBY:             handleStandby(); break;

		case State::LEADER_WAIT_NETWORK:  handleLeaderWaitNetwork(); break;
        case State::COMMISSIONER_START:   handleCommissionerStart(); break;
        case State::COMMISSIONER_ACTIVE:  handleCommissionerActive(); break;

        case State::JOINER_START:         handleJoinerStart(); break;
        case State::JOINER_SCAN:          handleJoinerScan(); break;
        case State::JOINER_WAIT_BROADCAST: handleJoinerWaitBroadcast(); break;
        case State::JOINER_WAIT_ACK:       handleJoinerWaitAck(); break;
        case State::JOINER_PAIRED:        handleJoinerPaired(); break;
        case State::JOINER_RECONNECT:     handleJoinerReconnect(); break;

        case State::ERROR:               handleError(); break;
        default:                         log_w("Unknown state"); break;
    }
}

void LightThread::handleInit() {
    if (justEntered) {
		justEntered = false;
        log_i("INIT: Entered initialization state.");

        if (role == Role::LEADER) {
            log_i("LEADER detected. Bootstrapping network setup...");

            execAndMatch("dataset init new", "Done");
            execAndMatch("dataset channel 11", "Done");
            execAndMatch("dataset panid 0x1234", "Done");
            execAndMatch("dataset networkkey 00112233445566778899aabbccddeeff", "Done");
            execAndMatch("dataset meshlocalprefix fd00::", "Done");
            execAndMatch("dataset commit active", "Done");
            execAndMatch("ifconfig up", "Done");
            execAndMatch("thread start", "Done");

            setState(State::LEADER_WAIT_NETWORK);
        } else {
            setState(State::STANDBY);
        }
    }
}

void LightThread::handleStandby() {}

void LightThread::handleError() {}

void LightThread::handleButton() {
    static bool buttonPressed = false;
    static unsigned long pressStart = 0;

    bool isPressed = digitalRead(buttonPin) == LOW;

    if (isPressed && !buttonPressed) {
        buttonPressed = true;
        pressStart = millis();
        log_d("Button press started");

    } else if (!isPressed && buttonPressed) {
        buttonPressed = false;
        unsigned long duration = millis() - pressStart;

        if (duration < 50) {
            log_d("Ignored press (debounce)");
            return;
        }

        if (duration >= 3000) {
            log_i("Long press");
            if (role == Role::JOINER) {
                clearPersistentState();
                setState(State::STANDBY);
            }
        } else {
            log_i("Short press");
            if (state == State::STANDBY) {
                setState(role == Role::LEADER ? State::COMMISSIONER_START : State::JOINER_START);
            }
        }
    }
}

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

        case State::COMMISSIONER_START:    blink(255, 140, 0); break;      // dark orange
        case State::COMMISSIONER_ACTIVE:   blink(0, 255, 0); break;        // blinking green

        case State::JOINER_START:          blink(0, 255, 255); break;      // cyan
        case State::JOINER_SCAN:           blink(135, 206, 250); break;    // light sky blue
        case State::JOINER_WAIT_BROADCAST: blink(0, 128, 255); break;      // bluish green
        case State::JOINER_WAIT_ACK:       blink(0, 128, 255); break;
        case State::JOINER_PAIRED:         set(0, 255, 0); break;          // solid green
        case State::JOINER_RECONNECT:      blink(255, 255, 0); break;      // blinking yellow

        case State::ERROR:                 blink(255, 0, 0); break;        // blinking red
        default:                           set(255, 0, 255); break;        // magenta (unknown)
    }
#endif
}
