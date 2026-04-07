#pragma once
#include "drivers/ifloat_sensor.hpp"

class MockFloatSensor : public IFloatSensor {
public:
    uint8_t percent_ = 80; // default: plenty of water

    uint8_t getPercent() const override
    {
        return percent_;
    }
};
