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
//   EP 24        Analog Input — battery voltage (V)
//   EP 25        Analog Input — PV voltage (V)
//   EP 26        Analog Input — PV power (W)
//   EP 27        Analog Input — controller temperature (deg C)
//   EP 28        Analog Input — reservoir water level (%)
//   EP 41        Analog Input cluster (FaultCode float 0.0–7.0)
//   EP 42        Analog Input cluster (charging status float 0.0–6.0)
//   EP 43        On/Off clear-fault command + Analog Input waterer state
//   EP 20        Also carries Analog Input active zone (0=none, 1–5=zone number)
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

    // Register outbound attribute reporting for all sensor attributes.
    // Must be called from the signal handler on ESP_ZB_BDB_SIGNAL_STEERING success
    // (inside the ZBOSS task context, before the scheduler fires) so that stale
    // reporting entries from a previous firmware version stored in zb_storage are
    // overwritten before ZBOSS fires them — preventing zcl_general_commands.c:612.
    static void configureReporting();

    // Push the on/off state for a zone endpoint (EP 10–14).
    // Zone is Running or Priming → on; Idle or Fault → off.
    static void reportZoneStatus(ZoneId zone, ZoneStatus status);

    // Push battery SOC (0-100 %) and voltage (V) to local Power Config attrs.
    // SOC is reportable through Power Config; voltage is also reported via EP24.
    static void reportBattery(uint8_t socPct, float voltageV);

    // Push the current fault code (0–7) to the Analog Input cluster.
    static void reportFault(FaultCode code);

    // Push periodic Renogy telemetry from real-time and historical registers.
    static void reportSolarData(float batteryVoltageV,
                                float pvVoltageV,
                                uint16_t pvPowerW,
                                float controllerTempC,
                                uint16_t maxChargingPowerW,
                                uint16_t dailyGenerationWh,
                                uint16_t dailyConsumptionWh,
                                uint8_t chargingStatus);

    // Push reservoir fill level as 0-100%.
    static void reportWaterLevel(uint8_t percent);

    // Push aggregate controller state for Home Assistant automation guards.
    static void reportWatererState(uint8_t stateCode);

    // Push active/faulted zone number (0=None, 1-5=ZoneId).
    static void reportActiveZone(uint8_t zoneNumber);

    // Reset the momentary clear-fault command endpoint to Off after handling it.
    static void resetClearFaultTrigger();

    // True after network steering succeeds.
    static bool isJoined();

    // True once the post-join interview grace period has elapsed.
    static bool reportsEnabled();

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
    static constexpr uint8_t kBatteryVoltEp    = 24u;
    static constexpr uint8_t kPvVoltEp         = 25u;
    static constexpr uint8_t kPvPowerEp        = 26u;
    static constexpr uint8_t kControllerTempEp = 27u;
    static constexpr uint8_t kWaterLevelEp     = 28u;
    static constexpr uint8_t kFaultEp          = 41u;
    static constexpr uint8_t kChargingStatusEp = 42u;
    static constexpr uint8_t kClearFaultEp     = 43u;
    static constexpr uint8_t kWatererStateEp   = kClearFaultEp;
    static constexpr uint8_t kActiveZoneEp     = kBatteryEp;

private:
    static void buildBasicEp(esp_zb_ep_list_t* ep_list);
    static void buildZoneEps(esp_zb_ep_list_t* ep_list);
    static void buildBatteryEp(esp_zb_ep_list_t* ep_list);
    static void buildSolarDataEps(esp_zb_ep_list_t* ep_list);
    static void buildFaultEp(esp_zb_ep_list_t* ep_list);
    static void buildChargingStatusEp(esp_zb_ep_list_t* ep_list);
    static void buildClearFaultEp(esp_zb_ep_list_t* ep_list);
};
