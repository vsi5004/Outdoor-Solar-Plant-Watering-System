#pragma once
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "watering/watering_fsm.hpp"

// Translates incoming ZCL commands into WateringFsm API calls.
//
// Call setFsm() before starting the Zigbee task.
// onAction() is registered by ZbDevice::init() via
// esp_zb_core_action_handler_register().
//
// On/Off cluster behaviour (EP 10–14):
//   On  command → fsm.request()  with the configured default duration
//   Off command → fsm.cancel()
class ZbHandlers {
public:
    // Bind to a live FSM instance and its mutex.
    // The mutex serialises FSM calls between this handler (Zigbee task) and the
    // watering task that calls tick().  defaultDurationSec is used for plain
    // "On" commands.
    static void setFsm(WateringFsm*     fsm,
                       SemaphoreHandle_t mutex,
                       uint32_t         defaultDurationSec = 15u);

    // Registered callback — invoked by the Zigbee stack for every ZCL action.
    static esp_err_t onAction(esp_zb_core_action_callback_id_t callback_id,
                               const void* message);

private:
    static WateringFsm*  fsm_;
    static SemaphoreHandle_t mutex_;
    static uint32_t      defaultDurationSec_;

    // Handle ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID — maps On/Off to FSM calls.
    static esp_err_t handleSetAttr(const esp_zb_zcl_set_attr_value_params_t* p);
};
