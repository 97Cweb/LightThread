#ifndef LIGHTTHREAD_H
#define LIGHTTHREAD_H

#include <Arduino.h>
#include "OThreadCLI.h"
#include "OThreadCLI_Util.h"

class Timer {
public:
    Timer(unsigned long timeoutMs) : timeout(timeoutMs), lastReset(millis()) {}

    void reset() { lastReset = millis(); }
    bool hasLapsed() const { return (millis() - lastReset) > timeout; }

private:
    unsigned long timeout;     // Timeout period in milliseconds
    unsigned long lastReset;   // Last reset time
};


class LightThread {
public:
    enum Role { LEADER, JOINER };
	
	enum LightThreadState {
		INITIALIZING,           				// Common initialization for both roles
		STANDBY,                				// Idle state
		COMMISSIONER_START,     				// Start commissioner mode (Leader only)
		COMMISSIONER_ACTIVE,    				// Active commissioner, listening and broadcasting (Leader only)
		JOINER_START,          					// Joiner attempts to start pairing
		JOINER_GET_NETWORK,						// Joiner gets network info from thread connection
		JOINER_LISTEN_FOR_BROADCAST,        	// Joiner listens for leader broadcast and sends ACK
		JOINER_WAIT_FOR_ACK,
		JOINER_PAIRED,          				// Successful pairing for the joiner
		JOINER_RECONNECT,						// get back on UDP
		ERROR
	};
	
	
	struct Joiner {
		String ip;     // Joiner's IP address
		uint16_t port; // Joiner's port
		String uniqueId; // Unique ID (hashed MAC)
		Timer timer{HEARTBEAT_TIMEOUT_MS};
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
	
	struct PacketInfo {
		String data;  		   // Pointer to the packet's data
		String destIp;         // Destination IP address
		uint16_t destPort;     // Destination port
		unsigned long timestamp; // Timestamp of when the packet was sent
		MessageType messageType;   // Type of the message (e.g., NORMAL, PAIR_ACK, etc.)
	};
	
	
	void processState();
	void initializeStateMachine();

    // Constructor
    LightThread(uint8_t buttonPin, Role role, const String& leaderIp = "", const String& networkKey = "", const String& networkName = "",
                int32_t channel = 11, uint16_t udpPort = 12345, const String& joinerKey = "J01NME",
                const String& panid = "0x1234");
				
	~LightThread() {
		clearJoiners();
	}
    // Public Methods
	//operation
    void begin();
	void update();
	LightThreadState getState() const;
	
	//communication
	bool setDestination(const String& uniqueId);
	bool sendData(const uint8_t* data, size_t length,AckType ackType);
	const char* getUniqueId() const ;
	bool isState(LightThreadState expectedState) const;
	
	
	std::function<void(const uint8_t* data, size_t len, const String& ip)> normalPacketHandler;

	void setNormalPacketHandler(std::function<void(const uint8_t*, size_t, const String&)> handler) {
		normalPacketHandler = handler;
	}




private:
    // Member Variables
    String networkKey;
    String networkName;
	String panid;
	int32_t channel;
	String joinerKey;
	uint16_t udpPort;
	
	String leaderIp;
    Timer leaderHeartbeatTimer{HEARTBEAT_TIMEOUT_MS}; // Timer for tracking leader's heartbeat

	
    uint8_t buttonPin;
	
	Role role;
	
	
	String destinationIp;
    uint16_t destinationPort;
	
	LightThreadState currentState;
	
	unsigned long timerStart = 0;    // For retries
    unsigned long timeoutStart = 0;  // For 1-minute commissioner and 10-second joiner timeouts


	std::vector<PacketInfo> pendingPackets;
    ot_cmd_return_t returnCode; // Shared structure for OpenThread command responses
	
	static constexpr int maxJoiners = 512;
	Joiner joinerTable[maxJoiners];
	int joinerCount = 0; // Number of currently saved joiners
	
	uint8_t lastReceivedAck[256]; // Adjust the size based on your protocol's maximum packet size
    size_t lastReceivedAckLength; // Length of the last received acknowledgment
	
	bool buttonStartFlag = false;       // Indicates that a button press has started
	unsigned long buttonPressStart = 0; // Time when the button was first pressed

	
	//magic numbers
	static constexpr size_t RESPONSE_BUFFER_SIZE = 256;
	static constexpr size_t MAX_HEX_PAYLOAD_LENGTH = RESPONSE_BUFFER_SIZE * 2;
	static constexpr size_t MAX_RECONNECT_TRIES = 16;
	static constexpr unsigned long NETWORK_TIMEOUT_MS = 5000;
	static constexpr unsigned long RETRY_INTERVAL_MS = 1000;
	static constexpr unsigned long REJOIN_TIMEOUT_MS = 120000;
	static constexpr unsigned long ACK_RETRY_TIMEOUT_MS = 3000;
	static constexpr unsigned long RETRY_TIMEOUT_MS = 20000;
	static constexpr unsigned long IS_LEADER_SCAN_DELAY_MS = 1000;
	static constexpr unsigned long COMMISSIONING_TIMEOUT_MS = 60000;
	static constexpr unsigned long COMMISSIONING_BROADCAST_RATE_MS = 3000;
	static constexpr unsigned long LOOP_DELAY_MS = 50;
	static constexpr unsigned long BLINK_TOGGLE_MS = 500;
	static constexpr unsigned long HEARTBEAT_TIMEOUT_MS = 30000; // 30 seconds timeout
	static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 5000; // 5 seconds interval
	
	
	// Timing thresholds for button press detection
	static constexpr unsigned long LONG_PRESS_THRESHOLD_MS = 3000; // 2 seconds for long press
	static constexpr unsigned long DEBOUNCE_DELAY_MS = 50; // 50 ms for debounce

	

	
	static constexpr char PAN_ID_SCAN_ALL[] = "0xffff";
	
	// UDP defaults
	static constexpr char MULTICAST_IP[] = "ff03::1";              // Multicast IP for broadcasting
	
	size_t numReconnectTries = 0;

	
	// Offsets for parsing network information
	static constexpr size_t NETWORK_KEY_OFFSET = 13;    // Offset to parse "Network Key" from response
	static constexpr size_t CHANNEL_OFFSET = 8;        // Offset to parse "Channel" from response
	static constexpr size_t PAN_ID_OFFSET = 7;         // Offset to parse "PAN ID" from response
	
	static constexpr uint8_t PAIR_REQUEST_BYTE = 0x01;  // Pairing request byte
	static constexpr uint8_t PAIR_ACK_BYTE = 0x02;      // Acknowledgment for pairing
	
	


    // Private Methods
	//Shared utilities
    bool setDataset(const String& command, const String& value);
    bool commitDataset();
    bool bringUpInterface();
    bool startThread();
	bool stopThread();
	
	// Role-specific initialization
    bool initializeRole();
    bool initializeLeader();
    bool initializeJoiner();
	
	//leader methods
	bool startCommissioner();
	bool addJoiner();
	void stopCommissioner();
	void saveJoiner(const String& ip, uint16_t port, const uint8_t* hash, size_t hashLength);
	void reconnectJoiner(const String& ip, uint16_t port, const uint8_t* hash, size_t hashLength);

    void clearJoiners(); // Clears all saved joiner data
	
	//joiner methods
	bool startJoin();
	bool checkJoinerState();
	bool getNetworkInfo();
	
	
	
    //state methods
	void setState(LightThreadState newState);
    void updateLighting();
	void setLightColor(int red, int green, int blue);
	void setLightBlink(int red, int green, int blue);
	
	
	//common handlers
	void handleInitializing();
	void handleStandby();
	void handleButton();
	void handleUdpActive();
	
	//leader handlers
	void handleCommissionerStart() ;
	void handleCommissionerActive();
	
	
	//joiner handlers
	void handleJoinerStart();
	void handleJoinerReconnect();
	void handleJoinerNetwork();
	void handleJoinerListenForBroadcast();
	void handleJoinerWaitForAck();
	void handleJoinerPaired();
	
	//udp methods
	bool openUDPSocket();
	void checkPendingAcks();
	bool receivedAckFor(const String&);
	bool sendUdpPacket(const uint8_t* data, size_t length, const String& destIp, uint16_t destPort, AckType ackType, MessageType messageType);
	bool parseResponseAsUDP(const char* response, String& srcIp, uint16_t& srcPort, uint8_t* message, size_t& messageLength, MessageType& messageType);
	String findUniqueIdByIp(const String& ip, uint16_t port) const;
	String getMeshLocalPrefix();
	void sendHeartbeats();
	void handleResponses();
	
	//CLI methods
	bool otGetResp(char* resp, size_t respSize, bool& isUDP);
	bool waitForString(String& responseBuffer, unsigned long timeoutMs, const String& thisString);
	void printMessage(const uint8_t* message, size_t messageLength);
	String convertToHexString(const uint8_t* data, size_t length) ;
	bool convertHexStringToBinary(const char* hexString, uint8_t* binary, size_t& binaryLength) ;
	
	//ids and hashing
	const char* hashMacAddress() const;
	
	//memory management
	bool saveNetworkInfo();
	bool saveLeaderIp();
	bool loadInfo();
	void clearNetworkInfo();
	
};

#endif // LIGHTTHREAD_H
