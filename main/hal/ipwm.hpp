#pragma once
#include <cstdint>

// Single PWM output channel.
// Duty is expressed as a percentage (0–100); the concrete implementation
// converts to the hardware duty register based on the configured resolution.
class IPwm {
public:
    virtual ~IPwm() = default;

    // Set duty cycle. duty must be in [0, 100].
    virtual void setDutyPercent(uint8_t duty) = 0;

    // Idle the channel (duty = 0). May differ from setDutyPercent(0) on
    // hardware where stopping the timer is preferable to running at 0%.
    virtual void stop() = 0;
};
