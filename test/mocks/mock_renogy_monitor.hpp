#pragma once
#include "drivers/irenogy_monitor.hpp"

class MockRenogyMonitor : public IRenogyMonitor {
public:
    RenogyData data_{};
    bool       setLoadResult_  = true;  // return value for setLoad()
    bool       lastLoadState_  = false; // last value passed to setLoad()
    int        setLoadCallCount_ = 0;

    MockRenogyMonitor()
    {
        data_.batterySoc = 80; // default: healthy battery
    }

    RenogyData getData() const override
    {
        return data_;
    }

    bool setLoad(bool on) override
    {
        lastLoadState_ = on;
        ++setLoadCallCount_;
        return setLoadResult_;
    }
};
