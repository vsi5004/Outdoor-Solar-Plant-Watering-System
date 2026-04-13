#pragma once
#include <cstdint>
#include "drivers/ipump_actuator.hpp"
#include "hal/ipwm.hpp"

// Controls the 12 V diaphragm pump through the pump BTS7960B half-bridge.
//
// LPWM is hardwired to GND on the board — the one-way valve makes reverse
// impossible and inadvertent reverse drive would stall the pump.
class PumpActuator : public IPumpActuator {
public:
    explicit PumpActuator(IPwm& rpwm);

    // duty: 0–100 %. Pass 100 for full-speed operation during dispensing.
    void setSpeed(uint8_t pct) override;
    void stop()                override;

private:
    IPwm& rpwm_;
};
