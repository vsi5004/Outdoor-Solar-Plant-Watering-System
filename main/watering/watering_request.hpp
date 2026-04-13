#pragma once
#include <cstdint>
#include "zone_id.hpp"

// Origin of a watering request — for logging and Zigbee status reporting.
enum class WaterSource : uint8_t {
    HaManual = 0,  // On/Off command from HA (manual trigger)
};

// Posted to the watering FSM queue by Zigbee handlers or the scheduler.
// All fields are set at enqueue time and never mutated by the FSM.
struct WateringRequest {
    ZoneId      zone;
    uint32_t    durationSec;
    WaterSource source;
};
