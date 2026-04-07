#pragma once
#include "hal/igpio.hpp"

// Test double for IGpio.
// Inspect public fields after exercising the system under test.
class MockGpio : public IGpio {
public:
    void setHigh() override
    {
        level_        = true;
        setHighCount_++;
    }

    void setLow() override
    {
        level_       = false;
        setLowCount_++;
    }

    bool getLevel() const override { return level_; }

    // ── Test inspection ──────────────────────────────────────────────────────
    bool level_       = false;
    int  setHighCount_ = 0;
    int  setLowCount_  = 0;
};
