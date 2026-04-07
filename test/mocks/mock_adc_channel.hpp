#pragma once
#include "hal/iadc_channel.hpp"

// Test double for IAdcChannel.
// Call setReturnValue() to configure the voltage the SUT will read.
class MockAdcChannel : public IAdcChannel {
public:
    explicit MockAdcChannel(float initialMv = 0.0f)
        : returnValue_(initialMv)
    {}

    void setReturnValue(float mv) { returnValue_ = mv; }

    float readMillivolts() override
    {
        readCount_++;
        return returnValue_;
    }

    // ── Test inspection ──────────────────────────────────────────────────────
    int   readCount_    = 0;
    float returnValue_  = 0.0f;
};
