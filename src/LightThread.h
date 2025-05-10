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
	NONE = 0x00,          // No ack needed (broadcast, simple fire-and-forget)
    REQUEST = 0x99,       // Receiver must reply
    RESPONSE = 0x98       // This is an ack/reply to REQUEST
};
	
enum MessageType {
	NORMAL = 0x00,     // Beeton payload
    PAIRING = 0x01,    // Used for initiating and confirming pairing
    RECONNECT = 0x02,  // Used for auto-reconnect flow
    HEARTBEAT = 0x03   // Regular ping/pong exchange
};

class LightThread {
public:
    LightThread(uint8_t buttonPin, Role role);

    void begin();
    void update();

    void setState(State newState);
    bool inState(State expected) const;

private:
    Role role;
    State state;
    unsigned long stateEntryTime = 0;
	bool justEntered = true;


    uint8_t buttonPin;

    void processState();
	unsigned long timeInState() const;


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
	bool otGetResp(String& lineOut, bool& isUDP, unsigned long timeoutMs = 100);
	bool waitForString(String& responseBuffer, unsigned long timeoutMs, const String& matchStr = "Done");

	void handleUdpLine(const String& line);
	void handleCliLine(const String& line);
	bool processCLIChar(char c, String& multiline, bool& isUDP, String& lineOut);

	uint16_t packMessage(AckType ack, MessageType type);
	void unpackMessage(uint16_t raw, AckType& ack, MessageType& type);
	bool parseIncomingPayload(const String& hex, AckType& ack, MessageType& type, std::vector<uint8_t>& payloadOut);
	uint64_t generateMacHash();
	bool sendUdpPacket(AckType ack, MessageType type, const uint8_t* payload, size_t length, const String& destIp, uint16_t destPort);
	bool sendUdpPacket(AckType ack, MessageType type, const std::vector<uint8_t>& payload, const String& destIp, uint16_t destPort);
	String extractUdpSourceIp(const String& line);
	
	bool convertHexToBytes(const String& hex, std::vector<uint8_t>& out);
	String convertBytesToHex(const uint8_t* data, size_t len);

};

#endif // LIGHTTHREAD_H
