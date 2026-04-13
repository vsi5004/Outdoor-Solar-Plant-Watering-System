#pragma once
#include "drivers/ipump_actuator.hpp"

class MockPumpActuator : public IPumpActuator {
public:
    uint8_t lastSpeed_     = 0;
    int     setSpeedCalls_ = 0;
    int     stopCalls_     = 0;

    void setSpeed(uint8_t pct) override
    {
        lastSpeed_ = pct;
        setSpeedCalls_++;
    }
    void stop() override
    {
        lastSpeed_ = 0;
        stopCalls_++;
    }
};
