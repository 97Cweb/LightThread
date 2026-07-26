#ifndef LIGHTTHREAD_H
#define LIGHTTHREAD_H

#include <Arduino.h>
#include <OThread.h>
#include <OThreadUDP.h>

#include "LightThreadConfig.h"
#include "LightThreadTypes.h"

#include <cstdint>
#include <functional>
#include <vector>


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

    bool goDormant();
    PowerMode getPowerMode() const{
        return configuredPowerMode;
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

  PowerMode configuredPowerMode = PowerMode::AWAKE;

  uint32_t configuredPollPeriodMs = LIGHTTHREAD_DEFAULT_POLL_PERIOD_MS;
  uint32_t configuredChildTimeoutSec = LIGHTTHREAD_DEFAULT_CHILD_TIMEOUT_SEC;
  uint32_t configuredDormantWakeSeconds = LIGHTTHREAD_DEFAULT_DORMANT_WAKE_SECONDS;

  bool configureJoinerPowerMode();

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
  void handleJoinerApplyPowerMode();
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
