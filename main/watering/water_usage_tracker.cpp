#include "watering/water_usage_tracker.hpp"

bool WaterUsageTracker::addDelivery(ZoneId zone, uint32_t milliliters)
{
    if (!isValidZoneId(zone) || milliliters == 0u)
    {
        return false;
    }

    totalsMl_[zoneIndex(zone)] += milliliters;
    return true;
}

void WaterUsageTracker::setTotalMl(ZoneId zone, uint64_t milliliters)
{
    if (!isValidZoneId(zone))
    {
        return;
    }

    totalsMl_[zoneIndex(zone)] = milliliters;
}

uint64_t WaterUsageTracker::getTotalMl(ZoneId zone) const
{
    if (!isValidZoneId(zone))
    {
        return 0u;
    }

    return totalsMl_[zoneIndex(zone)];
}

float WaterUsageTracker::getTotalLiters(ZoneId zone) const
{
    return static_cast<float>(getTotalMl(zone)) / 1000.0f;
}
