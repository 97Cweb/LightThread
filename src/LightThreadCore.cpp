#include "LightThread.h"

LightThread::LightThread() {
    pinMode(buttonPin, INPUT_PULLUP);
}

void LightThread::begin() {
    logLightThread(
        LIGHTTHREAD_LOG_INFO,
        "LightThread native OpenThread begin()"
    );

    thread.begin(false);
    setState(State::INIT);
}

void LightThread::update() {
    handleButton();
    receiveUdpPackets();
    processState();
    updateLighting();
}

void LightThread::setState(State newState) {
    if(state == newState) {
        return;
    }

    logLightThread(
        LIGHTTHREAD_LOG_INFO,
        "State transition: %d -> %d",
        static_cast<int>(state),
        static_cast<int>(newState)
    );

    state = newState;
    stateEntryTime = millis();
    justEntered = true;
    updateLighting();
}

bool LightThread::inState(State expected) const {
    return state == expected;
}

unsigned long LightThread::timeInState() const {
    return millis() - stateEntryTime;
}

bool LightThread::isThreadAttached() const {
    ot_device_role_t threadRole = thread.otGetDeviceRole();

    return threadRole == OT_ROLE_CHILD ||
           threadRole == OT_ROLE_ROUTER ||
           threadRole == OT_ROLE_LEADER;
}

void LightThread::refreshMyIp() {
    if(!isThreadAttached()) {
        myIp = IPAddress();
        return;
    }

    myIp = thread.getMeshLocalEid();

    logLightThread(
        LIGHTTHREAD_LOG_INFO,
        "MLEID: %s",
        myIp.toString().c_str()
    );
}

bool LightThread::openUdp() {
    if(udpOpen) {
        return true;
    }

    udpOpen = udp.begin(LIGHTTHREAD_UDP_PORT);

    if(!udpOpen) {
        logLightThread(
            LIGHTTHREAD_LOG_ERROR,
            "Could not bind UDP port %u",
            LIGHTTHREAD_UDP_PORT
        );

        return false;
    }

    logLightThread(
        LIGHTTHREAD_LOG_INFO,
        "UDP bound on port %u",
        LIGHTTHREAD_UDP_PORT
    );

    return true;
}

void LightThread::closeUdp() {
    if(!udpOpen) {
        return;
    }

    udp.stop();
    udpOpen = false;
}

void LightThread::processState() {
    switch(state) {
        case State::INIT:
            handleInit();
            break;

        case State::STANDBY:
            handleStandby();
            break;

        case State::LEADER_INIT:
            handleLeaderInit();
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

        case State::JOINER_WAIT_NETWORK:
            handleJoinerWaitNetwork();
            break;

        case State::JOINER_DISCOVER_LEADER:
            handleJoinerDiscoverLeader();
            break;

        case State::JOINER_PAIRED:
            handleJoinerPaired();
            break;

        case State::JOINER_RECONNECT:
            handleJoinerReconnect();
            break;

        case State::JOINER_FACTORY_RESET:
            handleJoinerFactoryReset();
            break;

        case State::ERROR:
            handleError();
            break;
    }
}

void LightThread::handleInit() {
    if(!justEntered) {
        return;
    }

    justEntered = false;

    if(!loadNetworkConfig()) {
        setState(State::ERROR);
        return;
    }

    if(role == Role::LEADER) {
        setState(State::LEADER_INIT);
        return;
    }

    if(thread.hasActiveDataset()) {
        logLightThread(
            LIGHTTHREAD_LOG_INFO,
            "Stored Thread dataset found"
        );

        setState(State::JOINER_RECONNECT);
        return;
    }

    logLightThread(
        LIGHTTHREAD_LOG_INFO,
        "Joiner has no stored Thread dataset"
    );

    setState(State::STANDBY);
}

void LightThread::handleStandby() {

    //leader normal ready state
}

void LightThread::handleError() {
    // Deliberately empty for now.
}

void LightThread::handleButton() {
    static bool buttonPressed = false;
    static unsigned long pressStart = 0;

    bool isPressed = digitalRead(buttonPin) == LOW;

    if(isPressed && !buttonPressed) {
        buttonPressed = true;
        pressStart = millis();

        return;
    }

    if(isPressed || !buttonPressed) {
        return;
    }

    buttonPressed = false;

    unsigned long duration = millis() - pressStart;

    if(duration < LIGHTTHREAD_BUTTON_DEBOUNCE_MS) {
        return;
    }

    if(duration >= LIGHTTHREAD_BUTTON_LONG_PRESS_MS) {
        if(role == Role::JOINER) {
            setState(State::JOINER_FACTORY_RESET);
        }

        return;
    }

    if(state == State::STANDBY) {
        setState(
            role == Role::LEADER
                ? State::COMMISSIONER_START
                : State::JOINER_START
        );
    }
}

// Updates the onboard RGB LED color based on current FSM state
void LightThread::updateLighting() {
#ifdef RGB_BUILTIN
    static unsigned long lastBlink = 0;
    static bool ledOn = false;

    auto set = [this](int r, int g, int b) {
        int colors[3] = {r, g, b}; // index 0=r,1=g,2=b
        int out[3];

        for (int i = 0; i < 3; i++) {
            switch (tolower(configuredLED[i])) {
                case 'r': out[i] = colors[0]; break;
                case 'g': out[i] = colors[1]; break;
                case 'b': out[i] = colors[2]; break;
                default:  out[i] = 0;         break; // safety fallback
            }
        }

        rgbLedWrite(RGB_BUILTIN, out[0], out[1], out[2]);
    };

    auto blink = [&](int r, int g, int b) {
        if(justEntered){
            ledOn = true;
            lastBlink = millis();
        }
        else if(millis() - lastBlink > LIGHTTHREAD_LED_BLINK_INTERVAL_MS) {
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

    case State::LEADER_INIT:
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
    case State::JOINER_WAIT_NETWORK:
        blink(135, 206, 250);
        break; // light sky blue
    case State::JOINER_DISCOVER_LEADER:
        blink(0, 128, 255);
        break; // bluish green
    case State::JOINER_PAIRED:
        set(0, 255, 0);
        break; // solid green
    case State::JOINER_RECONNECT:
        blink(255, 255, 0);
        break; // blinking yellow

    case State::ERROR:
        blink(255, 0, 0);
        break; // blinking red
    default:
        set(255, 0, 255);
        break; // magenta (unknown)
    }
#endif
}
