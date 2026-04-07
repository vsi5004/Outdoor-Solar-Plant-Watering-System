#pragma once
#include <array>
#include "watering/izone_manager.hpp"
#include "watering/zone_id.hpp"
#include "drivers/solenoid_actuator.hpp"

// Maps ZoneId values to the SolenoidActuator instances they control.
//
// Owns no solenoids — all references must outlive this object.
// Not thread-safe; call sites are responsible for serialisation if needed.
class ZoneManager : public IZoneManager {
public:
    // solenoids[0] controls Zone1, solenoids[1] → Zone2, …, solenoids[4] → Zone5.
    explicit ZoneManager(std::array<SolenoidActuator*, ZONE_COUNT> solenoids);

    void open(ZoneId id)        override;
    void close(ZoneId id)       override;
    void closeAll()             override;
    bool isOpen(ZoneId id) const override;

private:
    std::array<SolenoidActuator*, ZONE_COUNT> solenoids_;
};
