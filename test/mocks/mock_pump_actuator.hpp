#pragma once
#include "drivers/ipump_actuator.hpp"

class MockPumpActuator : public IPumpActuator {
public:
    uint8_t lastSpeed_    = 0;
    int     setSpeedCalls_ = 0;
    int     stopCalls_    = 0;
    float   currentMa_   = 800.0f; // default: healthy running current

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
    float readCurrentMa() const override
    {
        return currentMa_;
    }
};
