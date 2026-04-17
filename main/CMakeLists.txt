idf_component_register(
    SRCS
        "main.cpp"

        # HAL — concrete ESP-IDF implementations
        "hal/ledc_pwm.cpp"
        "hal/esp_adc_channel.cpp"
        "hal/esp_uart.cpp"
        "hal/esp_pcnt.cpp"
        "hal/esp_gpio.cpp"
        "hal/status_led.cpp"

        # Drivers
        "drivers/solenoid_actuator.cpp"
        "drivers/pump_actuator.cpp"
        "drivers/flow_meter.cpp"
        "drivers/float_sensor.cpp"
        "drivers/renogy_driver.cpp"

        # Watering logic
        "watering/zone_manager.cpp"
        "watering/watering_fsm.cpp"
        "watering/water_usage_tracker.cpp"

        # Zigbee layer
        "zb/zb_device.cpp"
        "zb/zb_handlers.cpp"

    INCLUDE_DIRS
        "."

    REQUIRES
        driver
        esp_adc
        esp_timer
        freertos
        nvs_flash
        espressif__esp-zigbee-lib
        espressif__led_strip
)
