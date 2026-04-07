#include "watering/zone_manager.hpp"

ZoneManager::ZoneManager(std::array<SolenoidActuator*, ZONE_COUNT> solenoids)
    : solenoids_(solenoids)
{
}

void ZoneManager::open(ZoneId id)
{
    solenoids_[zoneIndex(id)]->open();
}

void ZoneManager::close(ZoneId id)
{
    solenoids_[zoneIndex(id)]->close();
}

void ZoneManager::closeAll()
{
    for (auto* sol : solenoids_) {
        sol->close();
    }
}

bool ZoneManager::isOpen(ZoneId id) const
{
    return solenoids_[zoneIndex(id)]->isOpen();
}
