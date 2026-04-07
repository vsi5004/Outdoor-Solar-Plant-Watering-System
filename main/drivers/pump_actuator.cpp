#include "drivers/pump_actuator.hpp"
#include "config.hpp"

PumpActuator::PumpActuator(IPwm& rpwm, Bts7960Chip& chip, const SolenoidActuator& zone5Sol)
    : rpwm_(rpwm)
    , chip_(chip)
    , zone5Sol_(zone5Sol)
{}

void PumpActuator::setSpeed(uint8_t pct)
{
    rpwm_.setDutyPercent(pct);
}

void PumpActuator::stop()
{
    rpwm_.stop();
}

float PumpActuator::readCurrentMa() const
{
    float total = chip_.readCurrentMa();
    if (zone5Sol_.isOpen()) {
        total -= config::solenoid::HOLD_CURRENT_MA;
    }
    return total;
}
