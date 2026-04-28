#ifndef LIGHTTHREAD_H
#define LIGHTTHREAD_H

#include <Arduino.h>
#include <OThreadCLI.h> // must include full header

#include "LightThreadConfig.h"

#include <map>

// --- ENUM DEFINITIONS ---
enum class Role { LEADER, JOINER };

enum class State {
    INIT,
    STANDBY,

    // Joiner path
    JOINER_START,
    JOINER_SCAN,
    JOINER_WAIT_BROADCAST,
    JOINER_WAIT_RESPONSE,
    JOINER_PAIRED,
    JOINER_RECONNECT,
    JOINER_SEEKING_LEADER,
    JOINER_FACTORY_RESET,

    // Leader path
    LEADER_WAIT_NETWORK,
    COMMISSIONER_START,
    COMMISSIONER_ACTIVE,
    COMMISSIONER_STOPPING,

    ERROR
};

enum MessageType {  NORMAL =                0x00, 
                    PAIRING_BROADCAST =     0x01, 
                    PAIRING_REQUEST =       0x02, 
                    PAIRING_RESPONSE =      0x03, 
                    RECONNECT_REQUEST =     0x04, 
                    RECONNECT_RESPONSE =    0x05, 
                    HEARTBEAT =             0x06, 
                    HEARTBEAT_ECHO =        0x07 
                };

enum LightThreadLogLevel { LIGHTTHREAD_LOG_VERBOSE, LIGHTTHREAD_LOG_INFO, LIGHTTHREAD_LOG_WARN, LIGHTTHREAD_LOG_ERROR };

class LightThread {
  public:
    LightThread();

    void begin();  // LightThreadCore.cpp
    void update(); // LightThreadCore.cpp

    bool inState(State expected) const; // LightThreadCore.cpp
    String getLeaderIp() const { return leaderIp; }

    // ------------------------
    // exposedUDP.cpp
    // ------------------------
    // Exposed UDP (public-facing interface)
    void registerUdpReceiveCallback(
        std::function<void(const String &, const std::vector<uint8_t> &)> fn);
    void registerJoinCallback(std::function<void(const String &ip, const String &hashmac)> cb);

    bool sendUdp(const String &destIp, const std::vector<uint8_t> &payload);
    unsigned long getLastEchoTime(const String &ip);
    bool isReady() const;
    Role getRole() const { return role; }
    String getMyIp();

  private:
    // ------------------------
    // Variables: LightThread.h
    // ------------------------
    Role role = Role::JOINER; // default fallback
    State state;
    unsigned long stateEntryTime = 0;
    bool justEntered = true;
    uint8_t buttonPin;
    String leaderIp = ""; // Joiner: IP of the leader to reconnect to
    String myIp = "";


    //non blocking cli command tracking
    String pendingCliCommand;
    String pendingCliExpected;
    String pendingCliResponse;
    bool cliBusy = false;
    bool cliDone = false;
    bool cliFailed = false;
    unsigned long cliCommandStart = 0;
    unsigned long cliCommandTimeout = LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS;


    
    bool startCliCommand(const String& command, const String& expected, unsigned long timeoutMs = LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);
    void updateCliCommand();
    bool cliCommandDone();
    bool cliCommandFailed();
    String getCliResponse();
    void clearCliResult();


    // Data loaded from /network.json (DataStorage.cpp)
    int configuredChannel = -1;
    String configuredPrefix = "";
    String configuredPanid = "";

    // Heartbeat tracking (Joiner)
    unsigned long lastHeartbeatSent = 0;
    unsigned long lastHeartbeatEcho = 0;

    // Heartbeat tracking (Leader)
    std::map<String, unsigned long> joinerHeartbeatMap;
    std::function<void(const String &srcIp, const std::vector<uint8_t> &payload)>udpCallback = nullptr;
    std::function<void(const String &, const String &)> joinCallback = nullptr;



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
    void handleCommissionerStopping();

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
    void handleJoinerFactoryReset();

    int getJoinerSetupCommandCount() const;

    String getJoinerSetupCommand(int step);
    bool runJoinerSetupSequence(int &step, const char *logPrefix);

    void sendHeartbeatIfDue();

    // ------------------------
    // DataStorage.cpp
    // ------------------------
    bool loadNetworkConfig();
    bool parseNetworkJson(const String &jsonStr);
    void createDefaultNetworkConfig();
    bool saveLeaderInfo(const String &ip, const String &hashmac);
    bool loadLeaderInfo(String &outIp, String &outHashmac);
    void clearPersistentState();

    // ------------------------
    // CLI.cpp
    // ------------------------
    void readCliSerial();
    void handleCliLine(const String &line);
    bool processCLI(char c, String &multiline, bool &isUDP, String &lineOut, String &srcIpOut);

    // ------------------------
    // UDPComm.cpp
    // ------------------------
    void handleUdpLine(const String &line, const String & srcIp);
    bool sendUdpPacket(MessageType type, const uint8_t *payload, size_t length,
                       const String &destIp, uint16_t destPort);
    bool sendUdpPacket(MessageType type, const std::vector<uint8_t> &payload,
                       const String &destIp, uint16_t destPort);
    bool parseIncomingPayload(const String &hex,  MessageType &type, std::vector<uint8_t> &payloadOut);
    uint64_t generateMacHash();
    void captureMyIpFromResponse(const String &response);
    // ------------------------
    // Utils.cpp
    // ------------------------
    bool convertHexToBytes(const String &hex, std::vector<uint8_t> &out);
    String convertBytesToHex(const uint8_t *data, size_t len);
    std::vector<uint8_t> hashToBytes(uint64_t hash);
    uint64_t bytesToHash(const std::vector<uint8_t> &bytes);
    String hashToString(uint64_t hash);
    void logLightThread(LightThreadLogLevel level, const char *fmt, ...);

    // ------------------------
    // exposedUDP.cpp
    // ------------------------
    // Exposed UDP (public-facing interface)
    void handleNormalUdpMessage(const String &srcIp, const std::vector<uint8_t> &payload);
};

#endif // LIGHTTHREAD_H
