#pragma once
#include <cstdint>
#include "drivers/iflow_meter.hpp"
#include "hal/ipulse_counter.hpp"
#include "config.hpp"

// Translates raw PCNT pulse counts into a calibrated volume.
//
// Usage in a watering cycle:
//   1. Call reset() when the pump starts.
//   2. Poll getPulses() > PUMP_PRIME_PULSE_COUNT to detect prime.
//   3. Poll getMilliliters() for live volume dispensed.
//
// ML_PER_PULSE must be calibrated empirically — see config.hpp.
class FlowMeter : public IFlowMeter {
public:
    // mlPerPulse defaults to config::flow::ML_PER_PULSE; override in tests or
    // if a different calibration is loaded from NVS at runtime.
    explicit FlowMeter(IPulseCounter& counter,
                       float mlPerPulse = config::flow::ML_PER_PULSE);

    // Reset the underlying pulse counter to zero.
    void    reset()                override;

    // Return the raw pulse count since the last reset().
    int32_t getPulses() const      override;

    // Return the calibrated volume in millilitres since the last reset().
    float   getMilliliters() const override;

private:
    IPulseCounter& counter_;
    float          mlPerPulse_;
};
