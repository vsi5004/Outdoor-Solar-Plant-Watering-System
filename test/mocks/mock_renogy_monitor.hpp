#pragma once
#include "drivers/irenogy_monitor.hpp"

class MockRenogyMonitor : public IRenogyMonitor {
public:
    RenogyData data_{};
    bool       setLoadResult_  = true;  // return value for setLoad()
    bool       lastLoadState_  = false; // last value passed to setLoad()
    int        setLoadCallCount_ = 0;
    int*       sequence_ = nullptr;
    int        loadOnOrder_ = 0;
    int        loadOffOrder_ = 0;

    MockRenogyMonitor()
    {
        data_.valid      = true; // simulate a driver that has polled at least once
        data_.batterySoc = 80;   // default: healthy battery
    }

    RenogyData getData() const override
    {
        return data_;
    }

    void attachSequence(int& sequence)
    {
        sequence_ = &sequence;
    }

    bool setLoad(bool on) override
    {
        lastLoadState_ = on;
        ++setLoadCallCount_;
        if (sequence_) {
            if (on) {
                loadOnOrder_ = ++(*sequence_);
            } else {
                loadOffOrder_ = ++(*sequence_);
            }
        }
        return setLoadResult_;
    }
};
