#ifndef LIGHTTHREAD_H
#define LIGHTTHREAD_H

#include <Arduino.h>
#include <OThread.h>
#include <OThreadUDP.h>

#include "LightThreadConfig.h"

#include <functional>
#include <vector>

enum class Role {
    LEADER,
    JOINER
};

enum class State {
    INIT,
    STANDBY,

    // Leader path
    LEADER_INIT,
    LEADER_WAIT_NETWORK,
    COMMISSIONER_START,
    COMMISSIONER_ACTIVE,
    COMMISSIONER_STOPPING,

    // Joiner path
    JOINER_START,
    JOINER_WAIT_NETWORK,
    JOINER_DISCOVER_LEADER,
    JOINER_PAIRED,
    JOINER_RECONNECT,
    JOINER_FACTORY_RESET,

    ERROR
};

enum class MessageType : uint8_t {
    NORMAL             = 0x00,
    PAIRING_BROADCAST  = 0x01,
    PAIRING_REQUEST    = 0x02,
    PAIRING_RESPONSE   = 0x03,
    DISCOVERY_REQUEST  = 0x04,
    DISCOVERY_RESPONSE = 0x05
};

enum LightThreadLogLevel {
    LIGHTTHREAD_LOG_VERBOSE,
    LIGHTTHREAD_LOG_INFO,
    LIGHTTHREAD_LOG_WARN,
    LIGHTTHREAD_LOG_ERROR
};

class LightThread {
public:
    LightThread();

    void begin();
    void update();

    bool inState(State expected) const;
    bool isReady() const;

    Role getRole() const {
        return role;
    }

    String getMyIp() const {
        return myIp.toString();
    }

    String getLeaderIp() const {
        return leaderIp.toString();
    }

    void registerUdpReceiveCallback(
        std::function<void(
            const String &srcIp,
            const std::vector<uint8_t> &payload
        )> fn
    );

    void registerJoinCallback(
        std::function<void(
            const String &ip,
            const String &hashmac
        )> cb
    );

    bool sendUdp(
        const String &destIp,
        const std::vector<uint8_t> &payload
    );

private:
  // Identity and state
  Role role = Role::JOINER;
  State state = State::INIT;

  unsigned long stateEntryTime = 0;
  bool justEntered = true;

  uint8_t buttonPin = LIGHTTHREAD_DEFAULT_BUTTON_PIN;

  IPAddress leaderIp;
  IPAddress myIp;

  // Native OpenThread API objects
  OpenThread thread;
  DataSet dataset;
  OThreadUDP udp;

  bool udpOpen = false;

  // Configuration loaded from SD
  uint8_t configuredChannel = LIGHTTHREAD_DEFAULT_CHANNEL;
  uint16_t configuredPanId = LIGHTTHREAD_DEFAULT_PANID;
  String configuredPrefix = LIGHTTHREAD_DEFAULT_MESH_PREFIX;
  String configuredLED = LIGHTTHREAD_DEFAULT_LED;



  std::function<void(
      const String &srcIp,
      const std::vector<uint8_t> &payload
  )> udpCallback = nullptr;

  std::function<void(
      const String &ip,
      const String &hashmac
  )> joinCallback = nullptr;

  // Core
  void setState(State newState);
  void processState();
  unsigned long timeInState() const;

  void handleInit();
  void handleStandby();
  void handleError();

  void handleButton();
  void updateLighting();

  // Leader
  void handleLeaderInit();
  void handleLeaderWaitNetwork();
  void handleCommissionerStart();
  void handleCommissionerActive();
  void handleCommissionerStopping();

  // Joiner
  void handleJoinerStart();
  void handleJoinerWaitNetwork();
  void handleJoinerDiscoverLeader();
  void handleJoinerPaired();
  void handleJoinerReconnect();
  void handleJoinerFactoryReset();


  // Thread helpers
  bool isThreadAttached() const;
  bool openUdp();
  void closeUdp();
  void refreshMyIp();

  // Native UDP
  void receiveUdpPackets();

  bool sendUdpPacket(
      MessageType type,
      const uint8_t *payload,
      size_t length,
      const IPAddress &destIp,
      uint16_t destPort
  );

  bool sendUdpPacket(
      MessageType type,
      const std::vector<uint8_t> &payload,
      const IPAddress &destIp,
      uint16_t destPort
  );

  void handleUdpPacket(
      const IPAddress &srcIp,
      MessageType type,
      const std::vector<uint8_t> &payload
  );

  // Storage
  bool loadNetworkConfig();
  bool parseNetworkJson(const String &jsonStr);
  void createDefaultNetworkConfig();
  void clearPersistentState();

  // Exposed UDP
  void handleNormalUdpMessage(
      const String &srcIp,
      const std::vector<uint8_t> &payload
  );

  // Utilities
  uint64_t generateMacHash();

  std::vector<uint8_t> hashToBytes(uint64_t hash);
  uint64_t bytesToHash(const std::vector<uint8_t> &bytes);
  String hashToString(uint64_t hash);

  void logLightThread(
      LightThreadLogLevel level,
      const char *fmt,
      ...
  );
};

#endif
