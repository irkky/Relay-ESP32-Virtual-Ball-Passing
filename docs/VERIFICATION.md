# Verification record

Project implemented and verified in this workspace on 1 October 2026.

## ESP32 compilation

- **PASS:** PlatformIO `esp32dev`, Espressif32 **6.9.0**, Arduino-ESP32 **2.0.17**, ArduinoJson **6.21.5**.
- Real Xtensa ESP32 compilation and linking completed; `firmware.bin` produced.
- Application uses approximately **58%** of the default app partition and **14%** of static RAM capacity. Remaining RAM is also used dynamically by Wi-Fi/ESP-NOW at runtime.
- The common image contains both roles; station MAC selects the role.
- Arduino IDE installation instructions use the same core and library versions; the separate IDE GUI workflow has not been run here.

## Native game-engine tests

**PASS:** `scripts/test.ps1` compiles the actual `game.cpp` and `protocol.cpp` with C++11, `-Wall -Wextra -Werror`, then runs deterministic and randomized tests.

Verified:

- All seven initial targets and the required neighbor routes.
- Both invalid chain ends and inactive joystick input.
- Offline timeout and all-seven reset barrier.
- Independent loss of offer, receipt ACK, READY, RELEASE, GRANT, and GRANT_ACK.
- Retry exhaustion, same-transaction recovery, and a pass before activation ACK reaches the Master.
- Duplicate deliveries and stale packet replay including old initial grants.
- Master, owner, neighbor, and unrelated slave restarts; reset across a partition; in-flight receiver restart.
- Persistent-storage failure prevents new session creation.
- CRC, packet length, source/destination mismatch, non-neighbor routes, and boot fencing.
- Joystick neutral arming, debounce, ignored vertical input, and one gesture per held movement.
- **40 reproducible random seeds**, each driving 300 game steps with 22% injected loss, duplication, reordered delivery, historical packet replay, and reset. The at-most-one-active-slave invariant is checked after individual deliveries and ticks.

This is strong automated coverage, not a formal proof against every possible fault or hostile device.

## Dashboard and serial tests

- **PASS:** JavaScript syntax checks.
- **PASS:** Native JavaScript tests for partial/multiple lines, malformed boot text, oversized-line recovery, ordered writes, and disconnected writes.
- **PASS:** Real, headless installed Google Chrome running the actual local dashboard with a **mock Web Serial port**.
- Browser checks cover 8-card rendering, initial disabled controls, connection, selected start, changing holder, single active highlight, offline holder, stale-stream controls, reset, normal disconnect, USB-unplug simulation, reconnect, and rejecting a slave USB identity.
- Desktop (1440 px) and narrow (390 px) screenshots inspected; no horizontal overflow and no uncaught page errors in the browser suite.
- Screenshots are generated at `build/dashboard-desktop.png` and `build/dashboard-mobile.png`. The pictured live state is test data from the mocked port.

## Not yet physically verified

No ESP32 boards were flashed or physically exercised in this run. Actual radio range/loss, USB bridge behavior, joystick centers/orientation, pin mappings, power stability, LED wiring, and persistent storage on the user's eight boards require the bench procedure in [TESTING.md](TESTING.md).

The software implementation and build are complete; physical acceptance testing remains a hardware step.
