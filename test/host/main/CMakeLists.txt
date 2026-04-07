idf_component_register(
    SRCS
        # Entry point — calls run_xxx_tests() from each file below
        "test_runner.cpp"

        # Test suites
        "test_placeholder.cpp"
        "test_config.cpp"
        "test_mocks.cpp"
        "test_bts7960.cpp"
        "test_flow_meter.cpp"
        "test_float_sensor.cpp"
        "test_renogy.cpp"
        "test_zone_manager.cpp"
        "test_watering_fsm.cpp"

        # Driver sources under test (no hardware calls — injected via mocks)
        "../../../main/drivers/bts7960_chip.cpp"
        "../../../main/drivers/solenoid_actuator.cpp"
        "../../../main/drivers/pump_actuator.cpp"
        "../../../main/drivers/flow_meter.cpp"
        "../../../main/drivers/float_sensor.cpp"
        "../../../main/drivers/renogy_driver.cpp"
        "../../../main/watering/zone_manager.cpp"
        "../../../main/watering/watering_fsm.cpp"

    INCLUDE_DIRS
        "."
        "../../../main"
        "../../mocks"

    REQUIRES
        unity
        freertos
)
