#pragma once

#include <cstdint>

enum class Role {
    LEADER,
    JOINER
};

enum class PowerMode{
    AWAKE,
    SLEEPY,
    DORMANT
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
    JOINER_APPLY_POWER_MODE,
    JOINER_DISCOVER_LEADER,
    JOINER_PAIRED,
    JOINER_RECONNECT,
    JOINER_FACTORY_RESET,

    ERROR
};

enum class MessageType : uint8_t {
    NORMAL             = 0x00,
    PAIRING_REQUEST    = 0x01,
    PAIRING_RESPONSE   = 0x02,
    DISCOVERY_REQUEST  = 0x03,
    DISCOVERY_RESPONSE = 0x04
};

enum LightThreadLogLevel {
    LIGHTTHREAD_LOG_VERBOSE,
    LIGHTTHREAD_LOG_INFO,
    LIGHTTHREAD_LOG_WARN,
    LIGHTTHREAD_LOG_ERROR
};
