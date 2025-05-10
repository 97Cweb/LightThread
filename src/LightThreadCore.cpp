// LightThreadCore.cpp — fresh rewrite scaffold

#include "LightThread.h"
#include "OThreadCLI.h"

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
    updateLighting();
}

void LightThread::setState(State newState) {
    if (state != newState) {
        log_i("State transition: %d → %d", static_cast<int>(state), static_cast<int>(newState));
        state = newState;
        stateEntryTime = millis();
    }
}

bool LightThread::inState(State expected) const {
    return state == expected;
}

bool LightThread::justEnteredState(unsigned long thresholdMs) const {
    return (millis() - stateEntryTime) < thresholdMs;
}

bool LightThread::execAndMatch(const String& command, const String& mustContain, String* out, unsigned long timeoutMs) {
    log_d("CLI: %s", command.c_str());
    OThreadCLI.println(command);
    String response;
    if (!waitForString(response, timeoutMs, "Done")) {
        log_w("Command '%s' timed out", command.c_str());
        return false;
    }
    log_d("CLI response: %s", response.c_str());
    if (!mustContain.isEmpty() && response.indexOf(mustContain) == -1) {
        log_w("Unexpected response to '%s': %s", command.c_str(), response.c_str());
        return false;
    }
    if (out) *out = response;
    return true;
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
    static bool initialized = false;

    if (!initialized) {
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
        initialized = true;
    }
}

void LightThread::handleLeaderWaitNetwork() {
    static unsigned long lastCheck = 0;
    static int attempt = 0;

    if (millis() - lastCheck >= 5000) {
        lastCheck = millis();
        attempt++;

        String response;
        if (execAndMatch("state", "", &response, 1000)) {
            if (response.indexOf("leader") != -1 || response.indexOf("router") != -1) {
                log_i("LEADER_WAIT_NETWORK: Thread is up in state: %s", response.c_str());

                execAndMatch("udp open", "Done");
                execAndMatch("udp bind :: 12345", "Done");

                log_i("LEADER_WAIT_NETWORK: Transitioning to STANDBY");
                setState(State::STANDBY);
                attempt = 0;
                return;
            } else {
                log_i("LEADER_WAIT_NETWORK: Not a leader yet (attempt %d)", attempt);
            }
        } else {
            log_w("LEADER_WAIT_NETWORK: Failed to query state (attempt %d)", attempt);
        }

        if (attempt >= 10) {
            log_e("LEADER_WAIT_NETWORK: Timed out waiting for leader state");
            setState(State::ERROR);
        }
    }
}



void LightThread::handleStandby() {}
void LightThread::handleCommissionerStart() {
    static bool started = false;

    if (!started) {
        execAndMatch("commissioner start", "Commissioner: active");
        execAndMatch("commissioner joiner add * J01NME", "Joiner start");

        started = true;
        stateEntryTime = millis();
    }

    if (millis() - stateEntryTime > 1000) {
        log_i("COMMISSIONER_START: Setup complete. Transitioning to COMMISSIONER_ACTIVE");
        setState(State::COMMISSIONER_ACTIVE);
        started = false;
    }
}
void LightThread::handleCommissionerActive() {
    static bool udpReady = false;
    if (!udpReady) {
        execAndMatch("udp close", "Done");
        execAndMatch("udp open", "Done");
        execAndMatch("udp bind :: 12345", "Done");
        udpReady = true;
    }
    static unsigned long lastBroadcast = 0;
    const unsigned long broadcastInterval = 3000; // 3 seconds

    if (millis() - lastBroadcast > broadcastInterval) {
        lastBroadcast = millis();

        char payload[5];
        snprintf(payload, sizeof(payload), "%04x", MessageType::PAIR_REQUEST);

        String command = String("udp send ff03::1 12345 -x ") + payload;
        OThreadCLI.println(command);

        log_i("COMMISSIONER_ACTIVE: Sent PAIR_REQUEST broadcast");
    }

    handleUdpActive();
}
void LightThread::handleJoinerStart() {
	static bool joinStarted = false;

    if (!joinStarted) {
        log_i("JOINER_START: initializing joiner...");

        execAndMatch("dataset clear", "Done");
        execAndMatch("dataset panid 0xffff", "Done");
        execAndMatch("dataset channel 11", "Done");
        execAndMatch("dataset commit active", "Done");
        execAndMatch("ifconfig up", "Done");
        execAndMatch("udp open", "Done");
        execAndMatch("udp bind :: 12345", "Done");
        execAndMatch("joiner start J01NME", "Done");

        joinStarted = true;
        stateEntryTime = millis();
    }

    if (millis() - stateEntryTime > 500) {
        execAndMatch("thread start", "Done");
        log_i("JOINER_START: Thread start issued");
        setState(State::JOINER_SCAN);
        joinStarted = false;
    }
}
void LightThread::handleJoinerScan() {
    static bool scanStarted = false;
    static unsigned long retryTimer = 0;

    if (!scanStarted) {
        log_i("JOINER_SCAN: checking joiner state...");
        scanStarted = true;
        retryTimer = millis();
    }

    if (millis() - retryTimer >= 1000) {
        retryTimer = millis();
        String response;
        if (execAndMatch("joiner state", "", &response, 2000)) {
            log_d("Joiner state response: %s", response.c_str());
            if (response.indexOf("Join failed") == -1 && (response.indexOf("success") != -1 || response.indexOf("Idle") != -1)) {
                log_i("JOINER_SCAN: Joiner successfully paired");
                setState(State::JOINER_WAIT_BROADCAST);
                scanStarted = false;
                return;
            }
        } else {
            log_w("JOINER_SCAN: Failed to get joiner state");
        }
    }
}
void LightThread::handleJoinerWaitBroadcast() {
    static unsigned long listenStart = 0;
    static bool listening = false;

    if (!listening) {
        log_i("JOINER_WAIT_BROADCAST: Listening for leader broadcast...");
        listenStart = millis();
        listening = true;
    }

    if (millis() - listenStart > 20000) {
        log_w("JOINER_WAIT_BROADCAST: Timed out waiting for broadcast.");
        setState(State::STANDBY);
        listening = false;
        return;
    }

    // Handle any incoming UDP packets
    handleUdpActive();

    // Next step will be triggered by response parsing (PAIR_ACK → setState(JOINER_PAIRED))
}
void LightThread::handleJoinerWaitAck() {
    static unsigned long waitStart = 0;
    static bool waiting = false;

    if (!waiting) {
        log_i("JOINER_WAIT_ACK: Waiting for PAIR_ACK...");
        waitStart = millis();
        waiting = true;
    }

    if (millis() - waitStart > 10000) { // 10s timeout
        log_w("JOINER_WAIT_ACK: Timed out waiting for ACK");
        setState(State::STANDBY);
        waiting = false;
        return;
    }

    handleUdpActive(); // PAIR_ACK will cause a state transition to JOINER_PAIRED
}
void LightThread::handleJoinerPaired() {
    log_i("JOINER_PAIRED: storing configuration and entering standby");
    // In a real implementation: save leader IP, PAN ID, channel, etc.
    setState(State::STANDBY);
}
void LightThread::handleJoinerReconnect() {
    static bool reconnecting = false;

    if (!reconnecting) {
        log_i("JOINER_RECONNECT: attempting reconnection to mesh...");
        reconnecting = true;

        // Assume previously saved data exists
        execAndMatch("dataset commit active", "Done");
        execAndMatch("ifconfig up", "Done");
        execAndMatch("thread start", "Done");
        execAndMatch("udp open", "Done");
        execAndMatch("udp bind :: 12345", "Done");
    }

    handleUdpActive();
    // When ready, allow transition (e.g., after heartbeat or confirmation)
    // setState(State::STANDBY); <-- to be handled by message receipt
}
void LightThread::handleError() {}

void LightThread::handleUdpActive() {
    String line;
    if (!waitForString(line, 100, "")) return;  // short wait to avoid blocking

    if (line.indexOf("from ") == -1 || line.indexOf("12345") == -1) return;

    log_d("UDP Received: %s", line.c_str());

    char buffer[5];
    snprintf(buffer, sizeof(buffer), "%04x", MessageType::PAIR_REQUEST);
    if (line.indexOf(buffer) != -1 && inState(State::JOINER_WAIT_BROADCAST)) {
        log_i("JOINER_WAIT_BROADCAST: Received PAIR_REQUEST. Sending response...");
        OThreadCLI.println("udp send ff03::1 12345 -x 0022"); // Placeholder response
        setState(State::JOINER_WAIT_ACK);
        return;
    }

    snprintf(buffer, sizeof(buffer), "%04x", MessageType::PAIR_ACK);
    if (line.indexOf(buffer) != -1 && inState(State::JOINER_WAIT_ACK)) {
        log_i("JOINER_WAIT_ACK: Received PAIR_ACK.");
        setState(State::JOINER_PAIRED);
        return;
    }

    // Leader response to joiner hashed ID (simulated as 0022)
    if (line.indexOf("0022") != -1 && inState(State::COMMISSIONER_ACTIVE)) {
        log_i("COMMISSIONER_ACTIVE: Received joiner response, sending PAIR_ACK...");

        char ackPayload[5];
        snprintf(ackPayload, sizeof(ackPayload), "%04x", MessageType::PAIR_ACK);
        String ackCmd = String("udp send ff03::1 12345 -x ") + ackPayload;
        OThreadCLI.println(ackCmd);
    }
}



void LightThread::clearPersistentState() {
    log_i("[Stub] clearPersistentState() called. Implement this for NVS wipe.");
}

bool LightThread::waitForString(String& output, unsigned long timeoutMs, const char* endToken) {
    unsigned long start = millis();
    output = "";

    while (millis() - start < timeoutMs) {
        while (OThreadCLI.available()) {
            char c = OThreadCLI.read();
            output += c;
            Serial.print(c);  // Debug: show every incoming char
            if (output.indexOf(endToken) != -1) {
                return true;
            }
        }
        delay(10);
    }
    return false;
}


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
