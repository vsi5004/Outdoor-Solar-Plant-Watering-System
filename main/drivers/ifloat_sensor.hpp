#pragma once
#include <cstdint>

class IFloatSensor {
public:
    virtual ~IFloatSensor() = default;
    virtual uint8_t getPercent() const = 0;
};
