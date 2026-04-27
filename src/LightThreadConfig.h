#ifndef LIGHTTHREAD_CONFIG_H
#define LIGHTTHREAD_CONFIG_H

#include <Arduino.h>

// Pins
constexpr uint8_t LT_DEFAULT_BUTTON_PIN = 9;

// OpenThread / UDP
constexpr uint16_t LT_UDP_PORT = 12345;
constexpr const char* LT_MULTICAST_ALL_NODES = "ff03::1";
constexpr const char* LT_NETWORK_KEY = "00112233445566778899aabbccddeeff";
constexpr const char* LT_DEFAULT_PANID = "0x1234";
constexpr const char* LT_DEFAULT_PREFIX = "fd00::";
constexpr uint8_t LT_DEFAULT_CHANNEL = 11;

// CLI timing
constexpr unsigned long LT_CLI_DEFAULT_TIMEOUT_MS = 1000;
constexpr unsigned long LT_CLI_SHORT_TIMEOUT_MS = 2000;
constexpr unsigned long LT_CLI_JOIN_TIMEOUT_MS = 3000;

// State timing
constexpr unsigned long LT_LEADER_WAIT_TIMEOUT_MS = 50000;
constexpr unsigned long LT_COMMISSIONING_TIMEOUT_MS = 60000;
constexpr unsigned long LT_JOINER_START_TIMEOUT_MS = 20000;
constexpr unsigned long LT_JOINER_SCAN_TIMEOUT_MS = 10000;
constexpr unsigned long LT_JOINER_RECONNECT_TIMEOUT_MS = 120000;

constexpr unsigned long LT_STATE_QUERY_INTERVAL_MS = 1000;
constexpr unsigned long LT_SLOW_STATE_QUERY_INTERVAL_MS = 5000;
constexpr unsigned long LT_RECONNECT_STATE_QUERY_INTERVAL_MS = 2000;

// Heartbeat timing
constexpr unsigned long LT_HEARTBEAT_INTERVAL_MS = 5000;
constexpr unsigned long LT_HEARTBEAT_TIMEOUT_MS = 15000;
constexpr unsigned long LT_HEARTBEAT_REAPPEAR_MS = 10000;

// Button timing
constexpr unsigned long LT_BUTTON_DEBOUNCE_MS = 50;
constexpr unsigned long LT_BUTTON_LONG_PRESS_MS = 3000;

// LED timing
constexpr unsigned long LT_LED_BLINK_INTERVAL_MS = 500;

// Payload/hash sizes
constexpr size_t LT_HASH_BYTES = 8;
constexpr size_t LT_MAC_BYTES = 6;
constexpr size_t LT_MIN_PACKET_BYTES = 1;

// JSON/log buffers
constexpr size_t LT_NETWORK_JSON_CAPACITY = 512;
constexpr size_t LT_LEADER_JSON_CAPACITY = 256;
constexpr size_t LT_LOG_BUFFER_SIZE = 256;
constexpr size_t LT_HASH_STRING_BUFFER_SIZE = 17;

// FNV-1a 64-bit
constexpr uint64_t LT_FNV1A_OFFSET_BASIS = 0xcbf29ce484222325ULL;
constexpr uint64_t LT_FNV1A_PRIME = 1099511628211ULL;

#endif
