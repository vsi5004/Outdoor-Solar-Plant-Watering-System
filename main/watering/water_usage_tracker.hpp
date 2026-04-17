#pragma once

#include <array>
#include <cstdint>

#include "watering/zone_id.hpp"

// Accumulates per-zone flow-meter totals across watering cycles.
//
// Holds totals in RAM only. Callers are responsible for loading persisted
// values at boot (setTotalMl) and writing updated values to NVS after each
// successful addDelivery().
// Not thread-safe; all callers must be serialised (single wateringTask).
class WaterUsageTracker {
public:
    // Add a completed delivery for the given zone.
    // Returns true and increments the total when milliliters > 0.
    // Returns false without modifying state for a zero or invalid delivery.
    bool addDelivery(ZoneId zone, uint32_t milliliters);

    // Overwrite the persisted total for a zone (used to restore NVS values at boot).
    void setTotalMl(ZoneId zone, uint64_t milliliters);

    // Lifetime water total for a zone in milliliters (0 for invalid zones).
    uint64_t getTotalMl(ZoneId zone) const;
    // Lifetime water total for a zone in liters (0.0f for invalid zones).
    float getTotalLiters(ZoneId zone) const;

private:
    std::array<uint64_t, ZONE_COUNT> totalsMl_{};
};
