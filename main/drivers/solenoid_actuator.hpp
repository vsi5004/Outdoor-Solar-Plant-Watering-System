#pragma once
#include <cstdint>
#include "hal/ipwm.hpp"
#include "config.hpp"

// Controls one 12 V normally-closed solenoid valve through a BTS7960 half-bridge.
//
// Open sequence:
//   1. Drive PWM to 100 % for pullInMs — full voltage to pull the plunger in.
//   2. Drop to holdDutyPct % — enough to hold the plunger open, lower coil heat.
//
// close() stops the PWM and the spring-loaded plunger returns to closed.
//
// pullInMs and holdDutyPct default to the config values and can be overridden
// in tests to avoid real delays (pass pullInMs = 0 to skip vTaskDelay entirely).
class SolenoidActuator {
public:
    explicit SolenoidActuator(
        IPwm&    pwm,
        uint32_t pullInMs    = config::solenoid::PULL_IN_MS,
        uint8_t  holdDutyPct = config::solenoid::HOLD_DUTY_PCT
    );

    // Pull in then hold. Blocks for pullInMs before returning.
    void open();

    // De-energise the coil.
    void close();

    bool isOpen() const;

private:
    IPwm&    pwm_;
    uint32_t pullInMs_;
    uint8_t  holdDutyPct_;
    bool     open_ = false;
};
