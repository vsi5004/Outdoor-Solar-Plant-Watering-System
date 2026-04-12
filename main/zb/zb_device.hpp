#pragma once
#include <cstdint>
#include "watering/zone_id.hpp"
#include "watering/zone_status.hpp"
#include "watering/fault_code.hpp"
#include "esp_zigbee_core.h"

// Manages Zigbee stack lifecycle and ZCL attribute reporting.
//
// Endpoint map
//   EP  1        Basic + Identify clusters (mandatory Zigbee device)
//   EP 10–14     On/Off cluster, one per zone (Zone1 → EP10 … Zone5 → EP14)
//   EP 20        Power Configuration cluster (battery SOC + voltage from Renogy)
//   EP 21        Analog Input — max charging power today (W)
//   EP 22        Analog Input — daily solar generation (Wh)
//   EP 23        Analog Input — daily power consumption (Wh)
//   EP 41        Multistate Input cluster (FaultCode, 8 states 0–7)
//   EP 42        Multistate Input cluster (charging status, 7 states 0–6)
//
// Typical usage in app_main:
//   ZbDevice::init();           // register endpoints and action handler
//   esp_zb_start(false);        // start Zigbee task (non-forming coordinator)
//   // later, from the watering task:
//   ZbDevice::reportZoneStatus(ZoneId::Zone1, ZoneStatus::Running);
//   ZbDevice::reportBattery(socPct, voltageV);
//   ZbDevice::reportFault(FaultCode::PrimeTimeout);
//   ZbDevice::reportSolarData(minBattV, genWh, conWh, chargingStatus);
class ZbDevice {
public:
    // Register all endpoints and the ZCL action handler.
    // Must be called before esp_zb_start().
    static void init();

    // Push the on/off state for a zone endpoint (EP 10–14).
    // Zone is Running or Priming → on; Idle or Fault → off.
    static void reportZoneStatus(ZoneId zone, ZoneStatus status);

    // Push battery SOC (0–100 %) and voltage (V) to the Power Config cluster.
    static void reportBattery(uint8_t socPct, float voltageV);

    // Push the current fault code (0–7) to the Multistate Input cluster.
    static void reportFault(FaultCode code);

    // Push daily solar performance data from the Renogy historical registers.
    static void reportSolarData(uint16_t maxChargingPowerW,
                                uint16_t dailyGenerationWh,
                                uint16_t dailyConsumptionWh,
                                uint8_t chargingStatus);

    // Endpoint number for a given zone (EP = kZoneEpBase + zone_id value).
    static constexpr uint8_t zoneEp(ZoneId z)
    {
        return static_cast<uint8_t>(kZoneEpBase + static_cast<uint8_t>(z));
    }

    static constexpr uint8_t kZoneEpBase       = 9u;   // Zone1 → EP10 … Zone5 → EP14
    static constexpr uint8_t kBatteryEp        = 20u;
    static constexpr uint8_t kMaxChargePowerEp = 21u;
    static constexpr uint8_t kDailyGenEp       = 22u;
    static constexpr uint8_t kDailyConEp       = 23u;
    static constexpr uint8_t kFaultEp          = 41u;
    static constexpr uint8_t kChargingStatusEp = 42u;

private:
    static void buildBasicEp(esp_zb_ep_list_t* ep_list);
    static void buildZoneEps(esp_zb_ep_list_t* ep_list);
    static void buildBatteryEp(esp_zb_ep_list_t* ep_list);
    static void buildSolarDataEps(esp_zb_ep_list_t* ep_list);
    static void buildFaultEp(esp_zb_ep_list_t* ep_list);
    static void buildChargingStatusEp(esp_zb_ep_list_t* ep_list);
};
