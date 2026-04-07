#include "drivers/flow_meter.hpp"

FlowMeter::FlowMeter(IPulseCounter& counter, float mlPerPulse)
    : counter_(counter)
    , mlPerPulse_(mlPerPulse)
{}

void FlowMeter::reset()
{
    counter_.reset();
}

int32_t FlowMeter::getPulses() const
{
    return counter_.getCount();
}

float FlowMeter::getMilliliters() const
{
    return static_cast<float>(counter_.getCount()) * mlPerPulse_;
}
