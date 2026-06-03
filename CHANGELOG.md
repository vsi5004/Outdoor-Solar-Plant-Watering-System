# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.3] - 2026-06-03

### Fixed
- Suppressed locally echoed Zigbee attribute writes to keep device state from bouncing or duplicating updates
- Hardened Zigbee attribute updates and join-state tracking to better tolerate transient stack failures
- Added runtime guards around UART, status LED, and Renogy driver edge cases to reduce crash-prone paths

### Added
- Per-zone water usage tracking with NVS persistence across reboots
- Zigbee reporting of per-zone water totals to Home Assistant
- Ordered watering shutdown sequence to safely close valves before stopping pump
- Zigbee2MQTT Home Assistant integration with full zone control
- Status LED feedback for system state
- BTS7960 motor driver support for pump control
- 5-zone solenoid valve management via ZoneManager
- YF-S201 flow meter driver with configurable pulse-to-mL calibration
- Float sensor driver with configurable empty/full mV calibration points
- Renogy solar charge controller integration over Modbus RTU
- Watering FSM with safety interlocks (low water, prime detection)
- Unity host test suite covering all pure-logic modules
- CI pipeline with JUnit XML reporting via GitHub Actions
- GitHub release workflow with merged binary and individual partition artifacts
