#include "drivers/float_sensor.hpp"
#include <algorithm>

FloatSensor::FloatSensor(IAdcChannel& adc, float emptyMv, float fullMv)
    : adc_(adc)
    , emptyMv_(emptyMv)
    , fullMv_(fullMv)
{}

uint8_t FloatSensor::getPercent() const
{
    return getReading().percent;
}

WaterLevelReading FloatSensor::getReading() const
{
    const float mv = adc_.readMillivolts();
    const float ratio = (mv - emptyMv_) / (fullMv_ - emptyMv_);
    const float pct   = std::clamp(ratio, 0.0f, 1.0f) * 100.0f;
    // Add 0.5 before truncation to round to the nearest integer percentage.
    return {
        .millivolts = mv,
        .percent    = static_cast<uint8_t>(pct + 0.5f),
    };
}
