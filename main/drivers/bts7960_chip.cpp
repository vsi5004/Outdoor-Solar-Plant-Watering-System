#include "drivers/bts7960_chip.hpp"

Bts7960Chip::Bts7960Chip(IAdcChannel& isPin)
    : isPin_(isPin)
{}

float Bts7960Chip::readCurrentMa() const
{
    return isPin_.readMillivolts() * IS_MV_TO_MA;
}
