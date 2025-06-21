#ifndef LIGHTTHREAD_H
#define LIGHTTHREAD_H

#include <Arduino.h>
#include <OThreadCLI.h>  // must include full header
#include <optional>


#define BUTTON_PIN 9
#include <map>



// --- ENUM DEFINITIONS ---
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
    JOINER_SEEKING_LEADER,

    // Leader path
    LEADER_WAIT_NETWORK,
    COMMISSIONER_START,
    COMMISSIONER_ACTIVE,

    ERROR
};

enum AckType {
    NONE = 0x00,
    REQUEST = 0x99,
    RESPONSE = 0x98
};

enum MessageType {
    NORMAL = 0x00,
    PAIRING = 0x01,
    RECONNECT = 0x02,
    HEARTBEAT = 0x03
};

class LightThread {
public:
    LightThread();

    void begin();   // LightThreadCore.cpp
    void update();  // LightThreadCore.cpp

    bool inState(State expected) const;  // LightThreadCore.cpp
	String getLeaderIp() const { return leaderIp; }

	// ------------------------
	// exposedUDP.cpp
	// ------------------------
	// Exposed UDP (public-facing interface)
	void registerUdpReceiveCallback(std::function<void(const String&, const std::vector<uint8_t>&)> fn);
	void registerReliableUdpStatusCallback(std::function<void(uint16_t msgId, const String& ip, bool success)> cb);
	void registerJoinCallback(std::function<void(const String& ip, const String& hashmac)> cb);


	bool sendUdp(const String& destIp, bool reliable, const std::vector<uint8_t>& payload);
	std::map<String, String> getKnownJoiners();
	unsigned long getLastEchoTime(const String& ip);
	bool isReady() const;
	Role getRole() const { return role; }
	String getLeaderIp();
	
	
	
private:
    // ------------------------
    // Variables: LightThread.h
    // ------------------------
    Role role = Role::JOINER;  // default fallback
    bool roleLoadedFromConfig = false;
    State state;
    unsigned long stateEntryTime = 0;
    bool justEntered = true;
    uint8_t buttonPin;
    String leaderIp = "";  // Joiner: IP of the leader to reconnect to

    // Data loaded from /network.json (DataStorage.cpp)
    int configuredChannel = -1;
    String configuredPrefix = "";
    String configuredPanid = "";
	
	// Heartbeat tracking (Joiner)
	unsigned long lastHeartbeatSent = 0;
	unsigned long lastHeartbeatEcho = 0;

	// Heartbeat tracking (Leader)
	std::map<String, unsigned long> joinerHeartbeatMap;

	
	uint16_t nextMessageId = 0;

	struct PendingReliableUdp {
		String destIp;
		std::vector<uint8_t> payload;  // Includes messageId prepended
		unsigned long timeSent;
		uint8_t retryCount;
	};

	std::map<uint16_t, PendingReliableUdp> pendingReliableMessages;
	
	std::function<void(uint16_t, const String&, bool)> reliableCallback = nullptr;
	std::function<void(const String& srcIp, const std::vector<uint8_t>& payload)> udpCallback = nullptr;
	std::function<void(const String&, const String&)> joinCallback = nullptr;




    // ------------------------
    // LightThreadCore.cpp
    // ------------------------
    void setState(State newState);
	void processState();
    unsigned long timeInState() const;

    void handleInit();
    void handleStandby();
    void handleError();

    void handleButton();
    void updateLighting();

    // ------------------------
    // StateHandlers_Leader.cpp
    // ------------------------
    void handleLeaderWaitNetwork();
    void handleCommissionerStart();
    void handleCommissionerActive();

    // ------------------------
    // StateHandlers_Joiner.cpp
    // ------------------------
    void handleJoinerStart();
    void handleJoinerScan();
    void handleJoinerWaitBroadcast();
    void handleJoinerWaitAck();
    void handleJoinerPaired();
    void handleJoinerReconnect();
    void handleJoinerSeekingLeader();
	
    void sendHeartbeatIfDue();
    void setupJoinerDataset();
    void setupJoinerThreadDefaults();
	


    // ------------------------
    // DataStorage.cpp
    // ------------------------
    bool loadNetworkConfig();
    bool parseNetworkJson(const String& jsonStr);
    void createDefaultNetworkConfig();
	bool addJoinerEntry(const String& ip, const String& hashmac);
	bool isJoinerKnown(const String& hashmac);
	bool saveLeaderInfo(const String& ip, const String& hashmac);
	bool loadLeaderInfo(String& outIp, String& outHashmac);
	void clearPersistentState();



    // ------------------------
    // CLI.cpp
    // ------------------------
    bool execAndMatch(const String& command, const String& mustContain = "", String* out = nullptr, unsigned long timeoutMs = 1000);
    bool otGetResp(String& lineOut, bool& isUDP, unsigned long timeoutMs = 100);
    bool waitForString(String& responseBuffer, unsigned long timeoutMs, const String& matchStr = "Done");
    void handleCliLine(const String& line);
    bool processCLIChar(char c, String& multiline, bool& isUDP, String& lineOut);

    // ------------------------
    // UDPComm.cpp
    // ------------------------
    void handleUdpLine(const String& line);
    bool sendUdpPacket(AckType ack, MessageType type, const uint8_t* payload, size_t length, const String& destIp, uint16_t destPort, std::optional<uint16_t> messageId = std::nullopt);
    bool sendUdpPacket(AckType ack, MessageType type, const std::vector<uint8_t>& payload, const String& destIp, uint16_t destPort, std::optional<uint16_t> messageId = std::nullopt);
    String extractUdpSourceIp(const String& line);
    uint16_t packMessage(AckType ack, MessageType type);
    void unpackMessage(uint16_t raw, AckType& ack, MessageType& type);
    bool parseIncomingPayload(const String& hex, AckType& ack, MessageType& type, std::vector<uint8_t>& payloadOut);
    uint64_t generateMacHash();
    void updateReliableUdp();
    // ------------------------
    // Utils.cpp
    // ------------------------
    bool convertHexToBytes(const String& hex, std::vector<uint8_t>& out);
    String convertBytesToHex(const uint8_t* data, size_t len);
	
	// ------------------------
	// exposedUDP.cpp
	// ------------------------
	// Exposed UDP (public-facing interface)
	void handleNormalUdpMessage(const String& srcIp, const std::vector<uint8_t>& payload, AckType ack);


};

#endif // LIGHTTHREAD_H
