#pragma once
#include <cstdint>

// Per-zone status reported on Zigbee endpoints 10–14.
// Values are stable — do not reorder; Z2M converter and HA dashboards
// display integer descriptions (0=idle, 1=priming, 2=running, 3=fault).
enum class ZoneStatus : uint8_t {
    Idle    = 0,
    Priming = 1,
    Running = 2,
    Fault   = 3,
};
