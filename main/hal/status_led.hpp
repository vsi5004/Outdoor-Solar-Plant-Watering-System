#pragma once
#include <cstdint>

// Single WS2812 status LED driven by the RMT peripheral.
//
// States (set atomically from any task):
//   Booting  - slow amber pulse while waiting for Zigbee join
//   Joined   - brief green flash every 5 s (heartbeat)
//   Watering - solid cyan while a zone is active
//   Fault    - rapid red blink
//
// Call StatusLed::runTask() as a FreeRTOS task entry point.
class StatusLed {
public:
    enum class State : uint8_t {
        Booting,
        Joined,
        Watering,
        Fault,
    };

    // Initialise the RMT channel and LED strip.  Call once before runTask().
    static void init();

    // Set the current state from any task context (uses atomic store).
    static void setState(State s);

    // FreeRTOS task body; runs forever updating the LED every 100 ms.
    static void runTask(void* arg);

private:
    static void setPixel(uint8_t r, uint8_t g, uint8_t b);
    static void off();
};
