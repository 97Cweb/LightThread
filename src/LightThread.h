#ifndef LIGHTTHREAD_H
#define LIGHTTHREAD_H

#include <Arduino.h>
#include <OThreadCLI.h>  // must include full header



enum class Role {
    LEADER,
    JOINER
};

enum class State {
    INIT,
    STANDBY,

    // Joiner path
    JOINER_START,
    JOINER_SCAN,
    JOINER_WAIT_BROADCAST,
    JOINER_WAIT_ACK,
    JOINER_PAIRED,
    JOINER_RECONNECT,

    // Leader path
	LEADER_WAIT_NETWORK,
    COMMISSIONER_START,
    COMMISSIONER_ACTIVE,

    ERROR
};

enum AckType {
	NO_ACK = 0x00,    // No acknowledgment required
	ACK_REQUIRED = 0x99, // Acknowledgment required
	RETURN_ACK = 0x98,  // Returning acknowledgment
	ACK_REQUIRED_RETRY = 0x97 // Acknowledgment required but issued by retry, do not put in queue
};
	
enum MessageType {
	NORMAL = 0x00,
	PAIR_REQUEST = 0x01,     // Pairing request byte
	PAIR_ACK = 0x02,         // Acknowledgment for pairing
	RECONNECT_NOTIFY = 0x03, // Reconnecting message from joiner to leader
	HEARTBEAT = 0x04,
	UNKNOWN_MESSAGE = 0xFF   // Fallback for unrecognized messages
};

class LightThread {
public:
    LightThread(uint8_t buttonPin, Role role);

    void begin();
    void update();

    void setState(State newState);
    bool inState(State expected) const;
    bool justEnteredState(unsigned long thresholdMs = 250) const;

private:
    Role role;
    State state;
    unsigned long stateEntryTime = 0;

    uint8_t buttonPin;

    void processState();

    // Top-level state handlers
    void handleInit();
    void handleStandby();

    // Leader
	void handleLeaderWaitNetwork();
    void handleCommissionerStart();
    void handleCommissionerActive();

    // Joiner
    void handleJoinerStart();
    void handleJoinerScan();
    void handleJoinerWaitBroadcast();
    void handleJoinerWaitAck();
    void handleJoinerPaired();
    void handleJoinerReconnect();

    void handleError();

    // Core interaction
    void handleButton();
    void updateLighting();

    // Placeholder for long-press reset
    void clearPersistentState(); // to be implemented elsewhere
	
	bool execAndMatch(const String& command, const String& mustContain = "", String* out = nullptr, unsigned long timeoutMs = 1000);
	bool waitForString(String& output, unsigned long timeoutMs, const char* endToken);
	void handleUdpActive();

};

#endif // LIGHTTHREAD_H
