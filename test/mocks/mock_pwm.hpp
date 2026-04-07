#pragma once
#include <cstdint>
#include <vector>
#include "hal/ipwm.hpp"

// Test double for IPwm.
// Records every call to setDutyPercent and stop for assertion in tests.
class MockPwm : public IPwm {
public:
    // Full call history — use this when ordering matters (e.g. solenoid pull-in → hold).
    struct Call {
        enum class Kind : uint8_t { SetDuty, Stop };
        Kind    kind;
        uint8_t duty = 0; // only meaningful for SetDuty
    };

    void setDutyPercent(uint8_t duty) override
    {
        lastDuty_         = duty;
        setDutyCallCount_++;
        callLog_.push_back({Call::Kind::SetDuty, duty});
    }

    void stop() override
    {
        lastDuty_      = 0;
        stopCallCount_++;
        callLog_.push_back({Call::Kind::Stop, 0});
    }

    void reset()
    {
        lastDuty_         = 0;
        setDutyCallCount_  = 0;
        stopCallCount_     = 0;
        callLog_.clear();
    }

    // ── Test inspection ──────────────────────────────────────────────────────
    uint8_t          lastDuty_         = 0;
    int              setDutyCallCount_  = 0;
    int              stopCallCount_     = 0;
    std::vector<Call> callLog_;
};
