#pragma once
#include <cstdint>

class IPumpActuator {
public:
    virtual ~IPumpActuator() = default;
    virtual void setSpeed(uint8_t pct) = 0;
    virtual void stop()                = 0;
};
