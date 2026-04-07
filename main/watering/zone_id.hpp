#pragma once
#include <cstdint>

// Zone identifiers — use these everywhere a zone number is needed.
// The underlying value intentionally matches the Zigbee endpoint number (1–5).
enum class ZoneId : uint8_t {
    Zone1 = 1,
    Zone2 = 2,
    Zone3 = 3,
    Zone4 = 4,
    Zone5 = 5,
};

constexpr uint8_t ZONE_COUNT = 5;
constexpr uint8_t ZONE_ID_MIN = 1;
constexpr uint8_t ZONE_ID_MAX = 5;

constexpr bool isValidZoneId(ZoneId id)
{
    const auto v = static_cast<uint8_t>(id);
    return v >= ZONE_ID_MIN && v <= ZONE_ID_MAX;
}

// Index into a 0-based array from a ZoneId (Zone1 → 0, Zone5 → 4).
constexpr uint8_t zoneIndex(ZoneId id)
{
    return static_cast<uint8_t>(id) - 1u;
}
