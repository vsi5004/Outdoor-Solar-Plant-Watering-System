#pragma once
#include "watering/zone_id.hpp"

class IZoneManager {
public:
    virtual ~IZoneManager() = default;
    virtual void open(ZoneId id)       = 0;
    virtual void close(ZoneId id)      = 0;
    virtual void closeAll()            = 0;
    virtual bool isOpen(ZoneId id) const = 0;
};
