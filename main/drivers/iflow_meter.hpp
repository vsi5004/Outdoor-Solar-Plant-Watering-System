#pragma once
#include <cstdint>

class IFlowMeter {
public:
    virtual ~IFlowMeter() = default;
    virtual void    reset()                  = 0;
    virtual int32_t getPulses() const        = 0;
    virtual float   getMilliliters() const   = 0;
};
