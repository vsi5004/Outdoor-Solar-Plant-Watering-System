#pragma once
#include <cstdint>
#include "hal/ipulse_counter.hpp"

// Test double for IPulseCounter.
// Call setCount() to inject a pulse count; reset() zeroes it.
class MockPulseCounter : public IPulseCounter {
public:
    void reset() override
    {
        count_      = 0;
        resetCount_++;
    }

    int32_t getCount() const override
    {
        getCountCallCount_++;
        return count_;
    }

    void setCount(int32_t c) { count_ = c; }

    // ── Test inspection ──────────────────────────────────────────────────────
    int32_t count_             = 0;
    int     resetCount_        = 0;
    mutable int getCountCallCount_ = 0;
};
