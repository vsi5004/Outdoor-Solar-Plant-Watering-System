#pragma once
#include "drivers/irenogy_monitor.hpp"

class MockRenogyMonitor : public IRenogyMonitor {
public:
    RenogyData data_{};

    MockRenogyMonitor()
    {
        data_.batterySoc = 80; // default: healthy battery
    }

    RenogyData getData() const override
    {
        return data_;
    }
};
