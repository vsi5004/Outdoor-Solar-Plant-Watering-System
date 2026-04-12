set(MAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../main")

idf_component_register(
    SRCS
        "test_runner.cpp"
        "test_float_sensor.cpp"
        "${MAIN_DIR}/hal/esp_adc_channel.cpp"
        "${MAIN_DIR}/drivers/float_sensor.cpp"
    INCLUDE_DIRS
        "."
        "${MAIN_DIR}"
    REQUIRES
        unity
        esp_adc
)
