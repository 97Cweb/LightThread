#pragma once

#include <Arduino.h>
#include <openthread/dataset.h>
#include "LightThreadTypes.h"

// Hardware
constexpr uint8_t LIGHTTHREAD_DEFAULT_BUTTON_PIN = 9;

// Application UDP
constexpr uint16_t LIGHTTHREAD_UDP_PORT = 12345;

constexpr uint8_t LIGHTTHREAD_MULTICAST_ALL_NODES_BYTES[16] = {
    0xff, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01
};

// Thread network
constexpr char LIGHTTHREAD_NETWORK_NAME[] = "Beeton";
constexpr char LIGHTTHREAD_JOINER_PSKD[] = "J01NME";

constexpr uint8_t LIGHTTHREAD_DEFAULT_CHANNEL = 11;
constexpr uint16_t LIGHTTHREAD_DEFAULT_PANID = 0x1234;

constexpr char LIGHTTHREAD_DEFAULT_MESH_PREFIX[] = "fd00::";
constexpr char LIGHTTHREAD_DEFAULT_LED[] = "rgb";

constexpr uint8_t LIGHTTHREAD_NETWORK_KEY[OT_NETWORK_KEY_SIZE] = {
    0x00, 0x11, 0x22, 0x33,
    0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb,
    0xcc, 0xdd, 0xee, 0xff
};

constexpr uint8_t LIGHTTHREAD_EXTENDED_PAN_ID[OT_EXT_PAN_ID_SIZE] = {
    0x42, 0x65, 0x65, 0x74,
    0x6f, 0x6e, 0x00, 0x01
};

//sleepy and dormant
constexpr PowerMode LIGHTTHREAD_DEFAULT_POWER_MODE = PowerMode::AWAKE;

constexpr char LIGHTTHREAD_DEFAULT_POWER_MODE_TEXT[] =
    "awake";

constexpr uint32_t LIGHTTHREAD_DEFAULT_POLL_INTERVAL_MS = 500;
constexpr uint32_t LIGHTTHREAD_DEFAULT_CHILD_TIMEOUT_SEC = 300;

constexpr uint32_t LIGHTTHREAD_DEFAULT_DORMANT_WAKE_AFTER_SECONDS = 900;

/*
 * Brief time for a UDP datagram handed to OpenThread to leave the
 * application before Thread is shut down.
 *
 * This is not a reliable-message acknowledgement delay.
 */
constexpr uint32_t LIGHTTHREAD_DORMANT_TX_SETTLE_MS = 100;

// Timing
constexpr unsigned long LIGHTTHREAD_FAST_POLL_MS = 250;
constexpr unsigned long LIGHTTHREAD_NORMAL_POLL_MS = 1000;
constexpr unsigned long LIGHTTHREAD_SLOW_POLL_MS = 3000;

constexpr unsigned long LIGHTTHREAD_BUTTON_DEBOUNCE_MS = 50;
constexpr unsigned long LIGHTTHREAD_BUTTON_LONG_PRESS_MS = 3000;
constexpr unsigned long LIGHTTHREAD_LED_BLINK_INTERVAL_MS = 500;

constexpr unsigned long LIGHTTHREAD_LEADER_BROADCAST_INTERVAL_MS = 3000;

constexpr unsigned long LIGHTTHREAD_JOINER_START_TIMEOUT_MS = 60000;
constexpr unsigned long LIGHTTHREAD_JOINER_TIMEOUT_MS = 30000;
constexpr unsigned long LIGHTTHREAD_ATTACH_TIMEOUT_MS = 30000;

constexpr unsigned long LIGHTTHREAD_LEADER_TIMEOUT_MS = 50000;

constexpr uint32_t LIGHTTHREAD_COMMISSIONER_START_TIMEOUT_MS = 30000;
constexpr uint32_t LIGHTTHREAD_JOINER_WINDOW_SECONDS = 60;


// Packet and identity
constexpr size_t LIGHTTHREAD_UDP_HEADER_BYTES = 1;

constexpr size_t LIGHTTHREAD_HASH_BYTES = 8;
constexpr size_t LIGHTTHREAD_MAC_BYTES = 6;
constexpr size_t LIGHTTHREAD_HASH_STRING_BUFFER_SIZE = 17;

constexpr uint64_t LIGHTTHREAD_FNV1A_OFFSET_BASIS =
    0xcbf29ce484222325ULL;

constexpr uint64_t LIGHTTHREAD_FNV1A_PRIME =
    1099511628211ULL;

// Storage/logging
constexpr size_t LIGHTTHREAD_JSON_CAPACITY = 512;
constexpr size_t LIGHTTHREAD_LOG_BUFFER_SIZE = 256;

constexpr char LIGHTTHREAD_HASH_FORMAT[] = "%016llx";
