#pragma once
#include <cstdint>
#include "drivers/ipump_actuator.hpp"
#include "hal/ipwm.hpp"
#include "drivers/bts7960_chip.hpp"
#include "drivers/solenoid_actuator.hpp"

// Controls the 12 V diaphragm pump through the left half of BTS7960 board #1.
//
// LPWM is hardwired to GND on the board — the one-way valve makes reverse
// impossible and inadvertent reverse drive would stall the pump. There is
// therefore no reverse direction exposed in this class.
//
// IS correction (zone-5 sharing):
//   BTS7960 #1 shares its IS pin between the pump (left half) and solenoid 5
//   (right half). readCurrentMa() subtracts config::solenoid::HOLD_CURRENT_MA
//   from the raw chip reading whenever zone 5 is open, isolating pump current.
class PumpActuator : public IPumpActuator {
public:
    // zone5Sol: the solenoid-5 actuator that shares the IS pin on drv1.
    PumpActuator(IPwm&                  rpwm,
                 Bts7960Chip&           chip,
                 const SolenoidActuator& zone5Sol);

    // duty: 0–100 %. Pass 100 for full-speed operation during dispensing.
    void  setSpeed(uint8_t pct)  override;
    void  stop()                 override;

    // Returns pump-only current in mA, with zone-5 solenoid current subtracted
    // when solenoid 5 is open (shared IS pin correction).
    float readCurrentMa() const  override;

private:
    IPwm&                   rpwm_;
    Bts7960Chip&            chip_;
    const SolenoidActuator& zone5Sol_;
};
