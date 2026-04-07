#pragma once
#include <array>
#include "watering/izone_manager.hpp"

class MockZoneManager : public IZoneManager {
public:
    std::array<bool, ZONE_COUNT> open_{};
    int openCalls_     = 0;
    int closeCalls_    = 0;
    int closeAllCalls_ = 0;

    void open(ZoneId id) override
    {
        open_[zoneIndex(id)] = true;
        openCalls_++;
    }
    void close(ZoneId id) override
    {
        open_[zoneIndex(id)] = false;
        closeCalls_++;
    }
    void closeAll() override
    {
        open_.fill(false);
        closeAllCalls_++;
    }
    bool isOpen(ZoneId id) const override
    {
        return open_[zoneIndex(id)];
    }
};
