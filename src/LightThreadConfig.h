// LightThreadConfig.h

#pragma once
#include <Arduino.h>

constexpr uint8_t LIGHTTHREAD_DEFAULT_BUTTON_PIN = 9;

constexpr uint16_t LIGHTTHREAD_UDP_PORT = 12345;
constexpr const char* LIGHTTHREAD_MULTICAST_ALL_NODES = "ff03::1";
constexpr const char* LIGHTTHREAD_NETWORK_KEY = "00112233445566778899aabbccddeeff";
constexpr const char* LIGHTTHREAD_NETWORK_NAME = "OpenThreadMesh";
constexpr const char* LIGHTTHREAD_JOINER_PSKD = "J01NME";

// Base timing
constexpr unsigned long LIGHTTHREAD_FAST_POLL_MS = 250;
constexpr unsigned long LIGHTTHREAD_NORMAL_POLL_MS = 1000;
constexpr unsigned long LIGHTTHREAD_SLOW_POLL_MS = 3000;

// Human input / LED
constexpr unsigned long LIGHTTHREAD_BUTTON_DEBOUNCE_MS = 50;
constexpr unsigned long LIGHTTHREAD_BUTTON_LONG_PRESS_MS = LIGHTTHREAD_SLOW_POLL_MS;
constexpr unsigned long LIGHTTHREAD_LED_BLINK_INTERVAL_MS = 500;

// CLI
constexpr unsigned long LIGHTTHREAD_CLI_SERIAL_TIMEOUT_MS = LIGHTTHREAD_FAST_POLL_MS;
constexpr unsigned long LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS = LIGHTTHREAD_NORMAL_POLL_MS;
constexpr unsigned long LIGHTTHREAD_CLI_STATE_CHECK_INTERVAL_MS = LIGHTTHREAD_SLOW_POLL_MS;

// Thread setup / state checks
constexpr unsigned long LIGHTTHREAD_STATE_CHECK_INTERVAL_MS = LIGHTTHREAD_NORMAL_POLL_MS;
constexpr unsigned long LIGHTTHREAD_LEADER_BROADCAST_INTERVAL_MS = LIGHTTHREAD_SLOW_POLL_MS;
constexpr unsigned long LIGHTTHREAD_MODE_ESCALATION_DELAY_MS = 5000;

// Join / reconnect timeouts
constexpr unsigned long LIGHTTHREAD_JOINER_SCAN_TIMEOUT_MS = 10000;
constexpr unsigned long LIGHTTHREAD_JOINER_START_TIMEOUT_MS = 20000;
constexpr unsigned long LIGHTTHREAD_LEADER_TIMEOUT_MS = 50000;

// Heartbeat
constexpr unsigned long LIGHTTHREAD_HEARTBEAT_INTERVAL_MS = LIGHTTHREAD_MODE_ESCALATION_DELAY_MS;
constexpr unsigned long LIGHTTHREAD_HEARTBEAT_REAPPEAR_MS = LIGHTTHREAD_JOINER_SCAN_TIMEOUT_MS;
constexpr unsigned long LIGHTTHREAD_HEARTBEAT_TIMEOUT_MS = 3 * LIGHTTHREAD_HEARTBEAT_INTERVAL_MS;

constexpr uint8_t LIGHTTHREAD_DEFAULT_CHANNEL = 11;
constexpr const char* LIGHTTHREAD_DEFAULT_MESH_PREFIX = "fd00::";
constexpr const char* LIGHTTHREAD_DEFAULT_PANID = "0x1234";
constexpr const char* LIGHTTHREAD_DEFAULT_LED = "rgb";

constexpr size_t LIGHTTHREAD_HASH_BYTES = 8;
constexpr size_t LIGHTTHREAD_MAC_BYTES = 6;
constexpr size_t LIGHTTHREAD_HASH_STRING_BUFFER_SIZE = 17;

constexpr size_t LIGHTTHREAD_JSON_CAPACITY = 512;
constexpr size_t LIGHTTHREAD_LOG_BUFFER_SIZE = 256;

constexpr uint8_t LIGHTTHREAD_ROUTER_SELECTION_JITTER = 0;
constexpr uint8_t LIGHTTHREAD_ROUTER_UPGRADE_THRESHOLD = 255;
constexpr uint8_t LIGHTTHREAD_ROUTER_DOWNGRADE_THRESHOLD = 1;

constexpr uint8_t LIGHTTHREAD_UDP_HEADER_BYTES = 1;

constexpr uint64_t LIGHTTHREAD_FNV1A_OFFSET_BASIS = 0xcbf29ce484222325ULL;
constexpr uint64_t LIGHTTHREAD_FNV1A_PRIME = 1099511628211ULL;

constexpr const char* LIGHTTHREAD_CLI_UDP_FROM_MARKER = "bytes from ";
constexpr const char* LIGHTTHREAD_CLI_MLEID_COMMAND = "ipaddr mleid";
constexpr const char* LIGHTTHREAD_CLI_DONE = "Done";
constexpr const char* LIGHTTHREAD_THREAD_MODE = "rn";

constexpr const char* LIGHTTHREAD_MESH_LOCAL_PREFIX_START = "fd";
constexpr const char* LIGHTTHREAD_LINK_LOCAL_PREFIX_START = "fe80";


constexpr const char* LIGHTTHREAD_HASH_FORMAT = "%016llx";
constexpr const char* LIGHTTHREAD_HEX_CHARS = "0123456789abcdef";
