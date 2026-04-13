#pragma once
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "watering/zone_id.hpp"

// Command posted from the Zigbee callback to the watering task.
// The watering task drains this queue each tick, keeping the Zigbee
// stack task free regardless of how long FSM or I/O operations take.
struct ZbWateringCmd {
    enum class Type : uint8_t { Request, Cancel };
    Type     type;
    ZoneId   zone;        // Request only
    uint32_t durationSec; // Request only
};

// Translates incoming ZCL commands into ZbWateringCmd items on a queue.
//
// Call init() before starting the Zigbee task.
// onAction() is registered by ZbDevice::init() via
// esp_zb_core_action_handler_register().
//
// On/Off cluster behaviour (EP 10–14):
//   On  command → enqueues Request with the configured default duration
//   Off command → enqueues Cancel
class ZbHandlers {
public:
    // Bind to the command queue drained by the watering task.
    // defaultDurationSec is used for plain "On" commands.
    static void init(QueueHandle_t cmdQueue,
                     uint32_t      defaultDurationSec = 15u);

    // Registered callback — invoked by the Zigbee stack for every ZCL action.
    static esp_err_t onAction(esp_zb_core_action_callback_id_t callback_id,
                               const void* message);

private:
    static QueueHandle_t cmdQueue_;
    static uint32_t      defaultDurationSec_;

    // Handle ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID — enqueues Request or Cancel.
    static esp_err_t handleSetAttr(const esp_zb_zcl_set_attr_value_params_t* p);
};
