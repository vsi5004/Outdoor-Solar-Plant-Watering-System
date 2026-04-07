#pragma once
#include "drivers/iflow_meter.hpp"
#include "config.hpp"

class MockFlowMeter : public IFlowMeter {
public:
    int32_t pulses_     = 0;
    int     resetCalls_ = 0;

    void reset() override
    {
        pulses_ = 0;
        resetCalls_++;
    }
    int32_t getPulses() const override
    {
        return pulses_;
    }
    float getMilliliters() const override
    {
        return static_cast<float>(pulses_) * config::flow::ML_PER_PULSE;
    }
};
