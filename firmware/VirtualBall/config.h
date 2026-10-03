#pragma once
#include <stdint.h>

namespace cfg {
// Defaults: classic ESP32-WROOM/DevKit. Remap for S2/S3/C3 boards.
constexpr int JOY_X = 34, JOY_Y = 35, JOY_SW = 32;
constexpr int RED_PIN = 25, GREEN_PIN = 26, BLUE_PIN = 27;
constexpr bool COMMON_ANODE = false, INVERT_X = false;
constexpr int ADC_CENTER = 2048, DEADZONE = 900, CENTER_ZONE = 350;
constexpr uint32_t DEBOUNCE_MS = 65, SAMPLE_MS = 10, STUCK_MS = 15000;
constexpr uint8_t WIFI_CHANNEL = 1;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t ACK_TIMEOUT_MS = 350;
constexpr uint8_t RETRY_COUNT = 5; // initial attempt + five retries
constexpr uint32_t HEARTBEAT_MS = 2000, OFFLINE_MS = 7000, STATUS_MS = 1000;
constexpr uint32_t SERIAL_MAX_LINE = 384;
// Test bench only: 0 disables each fault. Type numbers are in protocol.h.
constexpr uint8_t DROP_TX_TYPE = 0, DROP_EVERY_N = 0, DUPLICATE_TX_TYPE = 0;
}
