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
//   EP 41        Multistate Input cluster (FaultCode, 7 states 0–6)
//
// Typical usage in app_main:
//   ZbDevice::init();           // register endpoints and action handler
//   esp_zb_start(false);        // start Zigbee task (non-forming coordinator)
//   // later, from the watering task:
//   ZbDevice::reportZoneStatus(ZoneId::Zone1, ZoneStatus::Running);
//   ZbDevice::reportBattery(socPct, voltageV);
//   ZbDevice::reportFault(FaultCode::PrimeTimeout);
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

    // Push the current fault code (0–6) to the Multistate Input cluster.
    static void reportFault(FaultCode code);

    // Endpoint number for a given zone (EP = kZoneEpBase + zone_id value).
    static constexpr uint8_t zoneEp(ZoneId z)
    {
        return static_cast<uint8_t>(kZoneEpBase + static_cast<uint8_t>(z));
    }

    static constexpr uint8_t kZoneEpBase = 9u;  // Zone1 → EP10 … Zone5 → EP14
    static constexpr uint8_t kBatteryEp  = 20u;
    static constexpr uint8_t kFaultEp    = 41u;

private:
    static void buildBasicEp(esp_zb_ep_list_t* ep_list);
    static void buildZoneEps(esp_zb_ep_list_t* ep_list);
    static void buildBatteryEp(esp_zb_ep_list_t* ep_list);
    static void buildFaultEp(esp_zb_ep_list_t* ep_list);
};
