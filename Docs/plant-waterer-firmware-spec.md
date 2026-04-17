# Solar Balcony Plant Watering System — Firmware Specification

## Overview

An ESP32-C6 WROOM coordinates a solar-powered, multi-zone balcony plant watering system. It joins the home Zigbee network as a router, communicates with Home Assistant through Zigbee2MQTT, and manages a pump, five solenoid valves, a flow meter, water level sensor, and a Renogy solar charge controller over Modbus RTU.

The firmware owns all timing and safety logic. Home Assistant writes optional per-zone watering durations and sends zone On/Off commands; the firmware executes the cycle and self-terminates. No watering cycle timing depends on wireless connectivity.

---

## Hardware Summary

### Microcontroller
- **ESP32-C6 WROOM** dev board
- Framework: **ESP-IDF v5.4+** (not Arduino — required for Zigbee SDK, PCNT, LEDC)
- Target: `idf.py set-target esp32c6`

### Power System
- **Renogy Wanderer 10A** solar charge controller
- **12V 46Ah LiFePO4** battery
- Communication: RS232 adapter → UART (Modbus RTU, 9600 baud 8N1)

### Water System
- **Reservoir**: Two interconnected 5-gallon buckets
- **Float sensor**: Variable resistance, read via ADC
- **Pump**: 12V self-priming DC diaphragm pump with one-way valve (unidirectional — no reverse needed or possible)
- **Flow meter**: Mechanical, square wave output, wired to PCNT input
- **Solenoids**: 5x 12V normally-closed solenoids (one per watering zone)

### Motor Drivers
- **3x BTS7960 43A H-Bridge Motor Driver boards**
  - Each chip is a dual half-bridge with a shared IS (current sense) pin
  - Board #1: Left half → Pump, Right half → Solenoid 5
  - Board #2: Left half → Solenoid 1, Right half → Solenoid 2
  - Board #3: Left half → Solenoid 3, Right half → Solenoid 4

---

## Pin Assignment (`config.hpp`)

All hardware mappings defined in one place. Based on the ESP32-C6-DevKitC-1 pinout.

**Pins reserved / avoided:**
- GPIO8 - onboard RGB status LED + boot strapping pin; do not wire an external load
- GPIO9 — boot strapping pin
- GPIO12/13 — USB D-/D+
- GPIO15 — JTAG (leave free during development)
- GPIO16/17 — U0TXD/U0RXD debug serial (needed for `idf.py monitor`)

**Enable wiring:** All six EN pins (L_EN + R_EN across all three boards) are wired together to a single GPIO. This gives a single firmware-controlled emergency shutoff for all motor drivers simultaneously.

**LPWM wiring:** The pump's LPWM pin is hardwired to GND on the board — the one-way valve makes reverse impossible.

```c
// UART / RS232 → Renogy Wanderer
#define RENOGY_UART_NUM     UART_NUM_1
#define RENOGY_TX_PIN       GPIO_NUM_4   // LP_UART_TXD capable
#define RENOGY_RX_PIN       GPIO_NUM_5   // LP_UART_RXD capable
#define RENOGY_BAUD         9600
#define RENOGY_MODBUS_ADDR  0x01

// Flow meter (PCNT hardware counter)
// GPIO15 used (external JTAG MTDI — safe as GPIO when no JTAG probe attached).
#define FLOW_METER_PIN      GPIO_NUM_15  // PCNT input

// Float sensor (ADC)
#define FLOAT_SENSOR_PIN    GPIO_NUM_0   // ADC1_CH0

// Master enable — all BTS7960 L_EN + R_EN pins wired together
#define DRV_MASTER_EN_PIN   GPIO_NUM_21  // Pull HIGH to enable all boards
                                         // Pull LOW to emergency-stop all output

// BTS7960 #1 — Left half: Pump | Right half: Solenoid 5
#define PUMP_LPWM_PIN       -1           // Hardwired to GND on board (no reverse)
#define PUMP_RPWM_PIN       GPIO_NUM_7   // Pump speed control PWM
#define SOL5_LPWM_PIN       -1           // Unused half — solenoid on right half only
#define SOL5_RPWM_PIN       GPIO_NUM_6   // Solenoid 5 PWM

// BTS7960 #2 — Left half: Solenoid 1 | Right half: Solenoid 2
#define SOL1_LPWM_PIN       GPIO_NUM_11  // Solenoid 1 PWM
#define SOL2_RPWM_PIN       GPIO_NUM_18  // Solenoid 2 PWM

// BTS7960 #3 — Left half: Solenoid 3 | Right half: Solenoid 4
#define SOL3_LPWM_PIN       GPIO_NUM_19  // Solenoid 3 PWM
#define SOL4_RPWM_PIN       GPIO_NUM_20  // Solenoid 4 PWM
```

**GPIO summary:**

| GPIO | Function | Notes |
|---|---|---|
| GPIO0 | Float sensor ADC | ADC1_CH0 |
| GPIO4 | UART1 TX → Renogy | LP_UART_TXD |
| GPIO5 | UART1 RX ← Renogy | LP_UART_RXD |
| GPIO6 | DRV1 RPWM — Sol5 | PWM |
| GPIO7 | DRV1 RPWM — Pump | PWM |
| GPIO11 | DRV2 LPWM — Sol1 | PWM |
| GPIO15 | Flow meter PCNT | JTAG MTDI — safe as GPIO when no probe attached |
| GPIO18 | DRV2 RPWM — Sol2 | PWM |
| GPIO19 | DRV3 LPWM — Sol3 | PWM |
| GPIO20 | DRV3 RPWM — Sol4 | PWM |
| GPIO21 | MASTER_EN all boards | All 6 EN pins wired to this |
| GPIO16 | U0TXD debug serial | Reserved — do not use |
| GPIO17 | U0RXD debug serial | Reserved — do not use |
| GPIO8 | RGB LED | Onboard status LED; boot strapping pin |
| GPIO9 | — | Reserved — boot strapping pin |
| GPIO12 | USB D- | Reserved |
| GPIO13 | USB D+ | Reserved |

---

## Watering Constants (`config.hpp`)

```cpp
namespace config::solenoid {
    constexpr uint8_t  PULL_IN_DUTY_PCT = 85;
    constexpr uint32_t PULL_IN_MS       = 100;
    constexpr uint8_t  HOLD_DUTY_PCT    = 25;
}

namespace config::pump {
    constexpr uint8_t  DUTY_PCT                  = 70;
    constexpr uint32_t PRIME_TIMEOUT_MS          = 15'000;
    constexpr uint32_t PRIME_PULSE_COUNT         = 5;
    constexpr uint32_t DEFAULT_WATERING_DURATION_SEC = 15;
    constexpr uint32_t MIN_WATERING_DURATION_SEC = 1;
    constexpr uint32_t MAX_WATERING_DURATION_SEC = 30u * 60u;
    constexpr uint32_t MAX_DISPENSE_MS           = 30u * 60u * 1'000u;
}

namespace config::safety {
    constexpr uint8_t MIN_BATTERY_SOC_PCT = 15;
    constexpr uint8_t MIN_WATER_LEVEL_PCT = 5;
}

namespace config::flow {
    constexpr float ML_PER_PULSE = 0.706f;
}

namespace config::sensor {
    constexpr float FLOAT_EMPTY_MV = 1390.0f;
    constexpr float FLOAT_FULL_MV  = 315.0f;
}

namespace config::renogy {
    constexpr uint32_t POLL_INTERVAL_MS      = 30'000;
    constexpr uint32_t RESPONSE_TIMEOUT_MS   = 500;
    constexpr uint32_t LOAD_ENABLE_SETTLE_MS = 4'500;
    constexpr uint32_t STALE_THRESHOLD_MS    = 3u * POLL_INTERVAL_MS;
}

namespace config::watering_sequence {
    constexpr uint32_t PUMP_STOP_TO_SOLENOID_CLOSE_MS =
        config::solenoid::PULL_IN_MS;
    constexpr uint32_t SOLENOID_CLOSE_TO_LOAD_DISABLE_MS =
        config::renogy::LOAD_ENABLE_SETTLE_MS;
}
```

---

## Project Structure

```text
plant-waterer/
|-- main/
|   |-- main.cpp                  # App entry, task creation, periodic reporting/logging
|   |-- config.hpp                # Pins, constants, thresholds, Zigbee identity
|   |-- drivers/
|   |   |-- pump_actuator.*        # Pump PWM actuator
|   |   |-- solenoid_actuator.*    # Solenoid pull-in/hold PWM actuator
|   |   |-- flow_meter.*           # PCNT pulse counter and ml calculation
|   |   |-- float_sensor.*         # ADC water level to percentage/mV reading
|   |   |-- renogy_driver.*        # Modbus RTU UART driver
|   |   `-- i*.hpp                 # Interfaces used by FSM tests
|   |-- hal/                       # ESP-IDF GPIO, ADC, PCNT, UART, LEDC adapters
|   |-- watering/
|   |   |-- watering_fsm.*         # Safety state machine
|   |   |-- water_usage_tracker.*  # Per-zone flow-meter total accumulator
|   |   |-- zone_manager.*         # Zone mapping/open/close helpers
|   |   |-- fault_code.hpp         # Stable HA/Z2M fault enum
|   |   `-- zone_*.hpp             # Zone id/status/request value types
|   `-- zb/
|       |-- zb_device.*            # Endpoint registration and attribute reports
|       `-- zb_handlers.*          # ZCL On/Off to command queue bridge
|-- zigbee2mqtt/
|   `-- solar-plant-waterer.mjs    # External converter deployed to Z2M
|-- test/
|-- partitions.csv                 # App + ESP Zigbee storage partition layout
|-- CMakeLists.txt
`-- sdkconfig
```

---

## Flash Partition Layout

`partitions.csv` is part of the firmware contract. The ESP Zigbee stack stores
network and factory state in named FAT data partitions, so the names and
subtypes matter.

| Name | Type | Subtype | Offset | Size | Purpose |
|---|---|---|---|---|---|
| `nvs` | data | nvs | `0x9000` | `0x6000` | ESP-IDF NVS |
| `phy_init` | data | phy | `0xf000` | `0x1000` | RF calibration data |
| `factory` | app | factory | `0x10000` | `0xf0000` | Main firmware image |
| `zb_storage` | data | fat | `0x100000` | `0x4000` | Zigbee network/reporting storage |
| `zb_fct` | data | fat | `0x104000` | `0x400` | Zigbee factory data |

---

## Drivers: Pump and Solenoids (`pump_actuator.*`, `solenoid_actuator.*`)

The pump and solenoid outputs are thin wrappers around the shared LEDC PWM HAL.
The watering FSM talks to them through `IPumpActuator` and `IZoneManager`, which
keeps the safety logic host-testable.

### Key Behaviors

**Pump:**
- LPWM is permanently tied low because the pump is one-way only
- RPWM is driven by `PumpActuator::setSpeed(percent)` using `config::pump::DUTY_PCT` during watering
- `PumpActuator::stop()` sets duty to 0 immediately

**Solenoids:**
- `SolenoidActuator::open()` applies `PULL_IN_DUTY_PCT` for `PULL_IN_MS`, then drops to `HOLD_DUTY_PCT`
- The watering startup sequence relies on that blocking pull-in interval so the pump is only started after the selected valve has had `PULL_IN_MS` to open
- `SolenoidActuator::close()` sets duty to 0
- `ZoneManager::closeAll()` is used by all fault, cancel, and normal-stop paths

**LEDC configuration:**
- Frequency: `config::ledc::FREQUENCY_HZ` (25 kHz)
- Resolution: `config::ledc::RESOLUTION_BITS` (10-bit duty range)
- Channels are assigned in `config::ledc` and kept one channel per active PWM output

Current-sense based dry-run detection is not part of the active firmware. Dry/empty
conditions are currently detected by reservoir level, Renogy freshness, battery SOC,
and prime-timeout flow pulses.

---

## Driver: Flow Meter (flow_meter.hpp/.cpp)

Uses the ESP-IDF **PCNT** (Pulse Counter) peripheral — hardware accelerated, zero CPU overhead.
Implemented as `EspPcnt` (HAL) + `FlowMeter` (driver).

- Counts rising edges on `FLOW_METER_PIN` (GPIO15)
- PCNT high limit: 32767, low limit: -32768, accumulate-across-overflow enabled
- `reset()` clears the hardware counter at pump start
- `getPulses()` → raw rising-edge count
- `getMilliliters()` → pulses × `ML_PER_PULSE`

**Calibration — `ML_PER_PULSE`:**

The sensor datasheet specifies **F = 23.6 × Q** where F is output frequency (Hz) and
Q is flow rate (L/min). Solving for volume per pulse:

```
mL/pulse = 1000 / (60 × 23.6) ≈ 0.706 mL/pulse
```

This is flow-rate independent (Q cancels). The configured value is **0.706 mL/pulse**
(datasheet-derived, ±3%). Verify empirically by pumping a known volume into a
measuring container and dividing by the pulse count.

**Use cases:**
- Prime detection: no pulses after pump start → still drawing air
- Volume metering: track mL dispensed per zone per session
- Anomaly detection:
  - Pulses with no solenoid open → leak
  - Solenoid open + pump running + no pulses → blockage or empty reservoir

---

## Driver: Float Sensor (float_sensor.hpp/.cpp)

- Variable resistance sensor on GPIO0 (ADC1_CH0)
- Use `adc_oneshot` API with `adc_cali` for voltage conversion
- Map calibrated voltage range to 0–100% water level
- Expose: `FloatSensor::getPercent()` → 0–100

### Circuit

Resistive voltage divider: 330 Ω fixed resistor from 3.3 V to the ADC pin,
sensor (variable resistance) from the ADC pin to GND.

```
3.3V
 │
[330Ω]
 │
 ├──── GPIO0 / ADC1_CH0
 │
[R_sensor]   30 Ω (empty) → 230 Ω (full)
 │
GND
```

Higher fill level → lower sensor resistance → lower ADC voltage.
(FLOAT_EMPTY_MV > FLOAT_FULL_MV — the FloatSensor math handles the inverted range.)

### Calibration (measured)

| Level | ADC voltage |
|-------|------------|
| Empty | 1390 mV    |
| Full  | 315 mV     |

Values stored in `config::sensor::FLOAT_EMPTY_MV` / `FLOAT_FULL_MV`.

### Oversampling

`EspAdcChannel` averages `config::adc::OVERSAMPLE_COUNT` (5) raw conversions per
`readMillivolts()` call to reduce ADC noise. Averaging is performed on raw counts
before the calibration curve is applied.

---

## Driver: Renogy Modbus RTU (renogy_driver.hpp/.cpp)

**Protocol:** Modbus RTU, 9600 baud 8N1, device address 0x01.

`poll()` issues three FC 0x03 read-holding-registers requests per cycle:

| Block | Start | Count | Fields |
|---|---|---|---|
| Real-time | 0x0100 | 10 | SOC, voltage, current, temp, load power, PV voltage/current/power |
| Historical | 0x010B | 10 | Max charging power today, daily generation Wh, daily consumption Wh |
| Status | 0x0120 | 1 | Charging status (low byte) |

**Real-time register map (block 0x0100–0x0109):**

| Register | Field | Scale |
|---|---|---|
| 0x0100 | Battery SOC | raw = % (0–100) |
| 0x0101 | Battery voltage | ×0.1 V |
| 0x0102 | Battery current | ×0.01 A, signed |
| 0x0103 high byte | Controller temperature | bit 7 = sign, bits 6:0 = °C magnitude |
| 0x0106 | Load power | W |
| 0x0107 | PV voltage | ×0.1 V |
| 0x0108 | PV current | ×0.01 A |
| 0x0109 | PV power | W |

**Historical register map (block 0x010B–0x0114, selected fields):**

| Register | Field | Scale |
|---|---|---|
| 0x010F | Max charging power today | W |
| 0x0113 | Daily power generation | Wh (unit = kWh/1000) |
| 0x0114 | Daily power consumption | Wh (unit = kWh/1000) |

**Status register (0x0120):**

| Bits | Field | Values |
|---|---|---|
| Low byte | Charging status | 0=not started, 1=startup, 2=MPPT, 3=equalisation, 4=boost, 5=float, 6=current limiting |

**Load output control:** FC 0x06 (write single register).
`setLoad(bool on)` first writes register `0xE01D = 0x000F` to set manual mode, then writes `0x010A = 0x0001` (on) or `0x0000` (off). Both writes validate the controller's echo response.

> **Note:** The Wanderer 10A echoes FC 0x06 frames for all addresses but does not
> persist the mode register write. Load mode must be set to Manual (0x0F) via the
> physical button on the controller before Modbus load commands take effect.

**Data snapshot struct:**

```cpp
struct RenogyData {
    uint16_t batterySoc;             // % (0–100)
    float    batteryVoltage;         // V
    float    batteryCurrent;         // A (positive = charging)
    float    pvVoltage;              // V
    float    pvCurrent;              // A
    uint16_t pvPower;                // W
    uint16_t loadPower;              // W
    float    controllerTemp;         // °C
    uint16_t maxChargingPowerToday;  // W
    uint16_t dailyGenerationWh;      // Wh
    uint16_t dailyConsumptionWh;     // Wh
    uint8_t  chargingStatus;         // 0–6 enum
    uint32_t lastUpdateMs;
};
```

**Implementation notes:**
- Dedicated UART peripheral (UART_NUM_1), not bit-banged
- CRC-16/IBM: polynomial 0xA001, init 0xFFFF
- Response timeout: 500 ms per block
- On any block failure `poll()` returns false and previously stored data is unchanged
- Thread-safe: `getData()` returns a mutex-protected copy; callable from any task

---

## Zone Mapping (`zone_manager.*`)

`ZoneManager` maps `ZoneId::Zone1` through `ZoneId::Zone5` to the five
solenoid actuators. The FSM never drives solenoids directly; it calls
`zones_.open(zone)`, `zones_.close(zone)`, or `zones_.closeAll()`.

| Zone | Driver board | Half | PWM pin | Notes |
|---|---|---|---|---|
| 1 | BTS7960 #2 | Left | GPIO11 | LPWM |
| 2 | BTS7960 #2 | Right | GPIO18 | RPWM |
| 3 | BTS7960 #3 | Left | GPIO19 | LPWM |
| 4 | BTS7960 #3 | Right | GPIO20 | RPWM |
| 5 | BTS7960 #1 | Right | GPIO6 | RPWM; shares board with pump |

Public surface used by the FSM:

```cpp
void ZoneManager::open(ZoneId zone);
void ZoneManager::close(ZoneId zone);
void ZoneManager::closeAll();
bool ZoneManager::isOpen(ZoneId zone) const;
```

---

## Watering State Machine (`watering_fsm.hpp/.cpp`)

Ticked by `wateringTask` at 10Hz. The FSM is intentionally small: all prechecks
happen before hardware is energized, and any fault path calls `stopAll()`.

### States

```cpp
enum class State : uint8_t {
    Idle,
    Priming,
    Watering,
    Fault,
};
```

### Context

```cpp
WateringRequest req_;
uint32_t        phaseStartMs_;
uint32_t        targetDurationMs_;
uint32_t        deliveredMl_;
bool            hasPendingDelivery_;
WateringDeliveryRecord pendingDelivery_;
FaultCode       fault_;
```

### Request Queue

```cpp
struct WateringRequest {
    ZoneId      zone;
    uint32_t    durationSec;
    WaterSource source;
};
```

### State Transition Logic

**`request(req, nowMs)`:**
- Reject unless the FSM is `Idle`
- Validate zone and duration; invalid input raises `FaultCode::InvalidRequest`
- Read the latest Renogy snapshot; absent or stale data raises `FaultCode::StaleData`
- Reject below `MIN_BATTERY_SOC_PCT` with `FaultCode::LowBattery`
- Reject below `MIN_WATER_LEVEL_PCT` with `FaultCode::LowWater`
- Enable the Renogy load output; failure raises `FaultCode::LoadEnableFailed`
- Wait one `config::solenoid::PULL_IN_MS` interval for the Renogy load path to settle
- Reset the flow meter, open the requested zone, let `zones_.open(req_.zone)` complete the solenoid pull-in interval, then start the pump and enter `Priming`

**`Priming`:**
- Wait until `flow.getPulses()` reaches `PRIME_PULSE_COUNT`
- If `PRIME_TIMEOUT_MS` expires first, raise `FaultCode::PrimeTimeout`
- On success, reset the phase timer and enter `Watering`

**`Watering`:**
- Stop normally when `targetDurationMs_` expires
- Raise `FaultCode::MaxDuration` if `MAX_DISPENSE_MS` is reached first
- Re-check Renogy freshness and battery SOC while running
- Record delivered milliliters from the flow meter on completion, cancel, or fault, and expose a one-shot `WateringDeliveryRecord` for `wateringTask` to accumulate
- Active shutdown is sequenced in reverse startup order: pump off, wait
  `PUMP_STOP_TO_SOLENOID_CLOSE_MS`, close all solenoids, wait
  `SOLENOID_CLOSE_TO_LOAD_DISABLE_MS`, then disable the Renogy load output

**`Fault`:**
- `enterFault()` records delivered volume, stops outputs, stores the fault code, and leaves the FSM latched in `Fault`
- Faults before any output is energized use the same output-off calls without the graceful shutdown delays
- `clearFault()` transitions `Fault` back to `Idle`
- `cancel()` stops an active `Priming` or `Watering` cycle and returns to `Idle`

### Safety Interlock (enforced in firmware regardless of HA state)
- Pump must NEVER run with all solenoids closed (dead-head risk)
- Pump starts only after the Renogy load settle delay and after `zones_.open(req_.zone)` completes its solenoid pull-in interval in `request()`
- `stopAll()` stops the pump first, closes all zones after the configured settle delay, and disables the Renogy load last
- Faults remain latched until an explicit clear command is received

---

## Fault Codes

```cpp
enum class FaultCode : uint8_t {
    None             = 0,
    LowBattery       = 1,  // Battery SOC below MIN_BATTERY_SOC_PCT
    LowWater         = 2,  // Reservoir below MIN_WATER_LEVEL_PCT
    PrimeTimeout     = 3,  // No flow pulses within config::pump::PRIME_TIMEOUT_MS
    MaxDuration      = 4,  // Watering exceeded MAX_DISPENSE_MS hard cap
    InvalidRequest   = 5,  // duration_sec == 0 or zone out of range
    LoadEnableFailed = 6,  // Renogy setLoad(true) returned false
    StaleData        = 7,  // Renogy data absent or older than STALE_THRESHOLD_MS
};
```

Values are stable: do not reorder. HA automations may key off the text value
published by the Zigbee2MQTT converter or the integer value reported on EP 41.

| Code | HA value | Meaning |
|---|---|---|
| 0 | `none` | No active fault |
| 1 | `battery_low` | Renogy SOC is at or below `MIN_BATTERY_SOC_PCT` |
| 2 | `water_low` | Reservoir level is at or below `MIN_WATER_LEVEL_PCT` |
| 3 | `prime_timeout` | Flow pulses did not arrive before `config::pump::PRIME_TIMEOUT_MS` |
| 4 | `max_duration` | Watering exceeded `MAX_DISPENSE_MS` |
| 5 | `invalid_request` | Bad zone or zero duration |
| 6 | `load_enable_failed` | Renogy `setLoad(true)` failed before pump start |
| 7 | `stale_data` | Renogy data is absent or older than `STALE_THRESHOLD_MS` |

Faults are latched by the watering FSM until cleared. Any zone `Off` command
cancels active watering and clears the latched fault; the Zigbee2MQTT converter
exposes this as a Home Assistant `Clear fault` button.

---

## Task Architecture (main.cpp)

```cpp
void app_main(void) {
    // Init order matters
    s_driver.init();
    s_flow.init();
    s_floatSensor.init();
    s_renogy.init();
    s_cmdQueue = xQueueCreate(8, sizeof(ZbWateringCmd));
    ZbHandlers::init(s_cmdQueue);  // initializes per-zone duration defaults

    xTaskCreate(zbTask,          "zb",      6144, nullptr, 5, nullptr);
    xTaskCreate(wateringTask,    "water",   4096, nullptr, 3, nullptr);
    xTaskCreate(renogyTask,      "renogy",  3072, nullptr, 2, nullptr);
    xTaskCreate(StatusLed::runTask, "led",  2048, nullptr, 1, nullptr);
}
```

| Task | Priority | Rate | Responsibility |
|---|---|---|---|
| `zbTask` | 5 | Event loop | Register endpoints, start Zigbee, run `esp_zb_stack_main_loop()` |
| `wateringTask` | 3 | 10Hz FSM tick | Drain Zigbee command queue, tick watering FSM, report zone/fault/water-level state |
| `renogyTask` | 2 | 1/30s | Modbus poll, report battery and solar telemetry |
| `StatusLed::runTask` | 1 | Internal blink cadence | Show boot, join, watering, and fault state |
| Zigbee stack | Internal | Event-driven | ZCL command callbacks and attribute reporting |

**Shared data access:**
- `RenogyDriver::getData()` returns a mutex-protected copy of the latest Modbus snapshot
- Zigbee commands are posted as `ZbWateringCmd` items so the Zigbee stack callback never blocks on watering or I/O work
- Zigbee attribute writes use `esp_zb_lock_acquire` / `esp_zb_lock_release`
- One-shot attribute reports are suppressed until the Zigbee interview grace period expires (`config::zigbee::REPORT_DELAY_AFTER_JOIN_MS`)

### Status LED

`StatusLed` drives the onboard ESP32-C6 WS2812 on GPIO8 through the ESP-IDF
`led_strip` RMT driver. State is stored in an atomic so `wateringTask`,
`renogyTask`, and Zigbee join handling can update it without queueing.

| Firmware state | LED behavior |
|---|---|
| Booting / joining | Slow blue blink |
| Joined / idle | Brief green heartbeat every 5 seconds |
| Watering | Solid cyan |
| Fault | Rapid red blink |

---

## Zigbee Interface (`zb_device.hpp/.cpp` + `zb_handlers.hpp/.cpp`)

### Design Principle

HA sends `On` or `Off` commands to the zone On/Off endpoints exposed by
Zigbee2MQTT. HA can also write a per-zone duration to each zone endpoint's
Analog Output cluster. An `On` command enqueues a watering request using that
zone's stored duration. An `Off` command cancels active watering and clears any
latched fault, which is how the HA `Clear fault` button works.

Firmware runs the cycle and self-terminates. HA receives live zone state,
per-zone lifetime water totals, fault state, battery data, solar telemetry,
charging status, and water level via Zigbee attribute reports.

### Endpoint Layout

| EP | Cluster | Role | Content |
|---|---|---|---|
| 1 | Basic + Identify | Server | Mandatory Zigbee device descriptors |
| 10 | On/Off + Analog Output | Server | Zone 1 active state and writable duration in seconds |
| 11 | On/Off + Analog Output | Server | Zone 2 active state and writable duration in seconds |
| 12 | On/Off + Analog Output | Server | Zone 3 active state and writable duration in seconds |
| 13 | On/Off + Analog Output | Server | Zone 4 active state and writable duration in seconds |
| 14 | On/Off + Analog Output | Server | Zone 5 active state and writable duration in seconds |
| 20 | Power Configuration + Analog Input | Server | `BatteryPercentageRemaining` (SOC × 2), `BatteryVoltage` (V × 10), active zone as float (`0.0`=none, `1.0`-`5.0`=zone number) |
| 21 | Analog Input | Server | Max charging power today (W) |
| 22 | Analog Input | Server | Daily solar generation (Wh) |
| 23 | Analog Input | Server | Daily power consumption (Wh) |
| 24 | Analog Input | Server | Battery voltage (V) |
| 25 | Analog Input | Server | PV voltage (V) |
| 26 | Analog Input | Server | PV power (W) |
| 27 | Analog Input | Server | Controller temperature (deg C) |
| 28 | Analog Input | Server | Reservoir water level (%) |
| 31 | Analog Input | Server | Zone 1 lifetime water total (L) |
| 32 | Analog Input | Server | Zone 2 lifetime water total (L) |
| 33 | Analog Input | Server | Zone 3 lifetime water total (L) |
| 34 | Analog Input | Server | Zone 4 lifetime water total (L) |
| 35 | Analog Input | Server | Zone 5 lifetime water total (L) |
| 41 | Analog Input | Server | `FaultCode` as float (`0.0` = None) |
| 42 | Analog Input | Server | Charging status as float (`0.0`=not started ... `5.0`=float ... `6.0`=current limiting) |
| 43 | On/Off + Analog Input | Server | Momentary clear-fault command plus waterer state as float (`0.0`=idle, `1.0`=priming, `2.0`=watering, `3.0`=fault) |

All Renogy data comes from the background `renogyTask` polling at 30s intervals.
Zone on/off states are pushed from `wateringTask` on every state change.
`waterer_state` and `active_zone` are also pushed from `wateringTask` whenever
the aggregate controller state changes. Zone water totals are pushed at join
after the reporting grace period and after each non-zero delivery.

### Status Enum

```cpp
enum class ZoneStatus : uint8_t {
    Idle    = 0,
    Priming = 1,
    Running = 2,
    Fault   = 3,
};
```

### Command Handler (`ZbHandlers`)

```cpp
struct ZbWateringCmd {
    enum class Type : uint8_t { Request, Cancel, ClearFault };
    Type     type;
    ZoneId   zone;
    uint32_t durationSec;
};

ZbHandlers::init(s_cmdQueue, 15);
```

`ZbHandlers::onAction()` is registered as the ESP Zigbee core action handler.
It translates On/Off writes on endpoints 10-14 into queue messages:

| ZCL command | Firmware action |
|---|---|
| Zone endpoint Analog Output `presentValue` write | Store that zone's duration, clamped to 1-1800 seconds |
| Zone endpoint `On` | Enqueue `Request` with that zone's stored duration |
| Zone endpoint `Off` | Enqueue `Cancel`; `wateringTask` also calls `clearFault()` |
| Clear-fault endpoint 43 `On` | Enqueue `ClearFault`; firmware resets EP43 back to Off |

If a zone `On` command arrives while another zone is already `Priming` or
`Running`, the FSM rejects it without raising a fault. The firmware reports the
newly requested inactive zone back to Off so the HA switch reflects reality.

The callback only posts to the queue. The watering task owns all slow work:
prechecks, Renogy load control, pump/solenoid sequencing, fault clearing, and
logs.

### Public Update Functions

```cpp
void ZbDevice::reportZoneStatus(ZoneId zone, ZoneStatus status);
void ZbDevice::reportZoneWaterTotal(ZoneId zone, uint64_t totalMilliliters);
void ZbDevice::reportBattery(uint8_t socPct, float voltageV);
void ZbDevice::reportSolarData(float batteryVoltageV,
                               float pvVoltageV,
                               uint16_t pvPowerW,
                               float controllerTempC,
                               uint16_t maxChargingPowerW,
                               uint16_t dailyGenerationWh,
                               uint16_t dailyConsumptionWh,
                               uint8_t chargingStatus);
void ZbDevice::reportWaterLevel(uint8_t percent);
void ZbDevice::reportFault(FaultCode code);
void ZbDevice::reportWatererState(uint8_t stateCode);
void ZbDevice::reportActiveZone(uint8_t zoneNumber);
bool ZbDevice::isJoined();
bool ZbDevice::reportsEnabled();
```

### Attribute Reporting Configuration

`ZbDevice::configureReporting()` is called after network steering succeeds. It
updates the ZBOSS reporting table for every reportable attribute so stale entries
from earlier firmware builds do not fire reports against removed endpoints or
attributes.

Sensor telemetry is pushed with one-shot `esp_zb_zcl_report_attr_cmd_req()`
calls after `reportsEnabled()` returns true. The post-join delay gives
Zigbee2MQTT time to finish the interview before the device starts sending
unsolicited reports.

Zone endpoints report On when the FSM state is `Priming` or `Running`, and Off
when the state is `Idle` or `Fault`. The firmware sends these updates on every
zone state change.

### Zigbee Device Config

```cpp
esp_zb_cfg_t zb_cfg = {
    .esp_zb_role         = ESP_ZB_DEVICE_TYPE_ROUTER,
    .install_code_policy = false,
    .nwk_cfg.zczr_cfg = {
        .max_children = 10,
    },
};

esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
```

Model string and manufacturer are set on the Basic cluster and are used by
Zigbee2MQTT to match the external converter:

| Field | Value |
|---|---|
| Model identifier | `solar-plant-waterer` |
| Manufacturer | `Ivanbuilds` |

Development recovery switch:

```cpp
config::zigbee::ERASE_NVRAM_ON_BOOT = false;
```

Set this to `true` for exactly one boot if stale Zigbee storage is causing bad
interviews or old reporting entries. Flash back to `false` immediately after the
device rejoins, otherwise the device will forget its network on every reboot.

---

## Zigbee2MQTT External Converter

The repository keeps the active converter at
`zigbee2mqtt/solar-plant-waterer.mjs`. Copy that file to the Zigbee2MQTT data
directory:

```text
data/external_converters/solar-plant-waterer.mjs
```

Restart Zigbee2MQTT and verify startup includes:

```text
Loaded external converter 'solar-plant-waterer.mjs'.
```

Converter identity:

| Field | Value |
|---|---|
| `zigbeeModel` | `solar-plant-waterer` |
| Z2M model | `solar-plant-waterer` |
| Vendor | `Ivanbuilds` |

The converter uses ESM imports from `zigbee-herdsman-converters`, maps endpoint
10-14 to `zone_1` through `zone_5`, and exposes zone controls with:

```js
m.deviceEndpoints({
    endpoints: {
        zone_1: 10,
        zone_2: 11,
        zone_3: 12,
        zone_4: 13,
        zone_5: 14,
    },
});

m.onOff({
    endpointNames: ['zone_1', 'zone_2', 'zone_3', 'zone_4', 'zone_5'],
    powerOnBehavior: false,
});
```

Analog Input reports are decoded by source endpoint:

| EP | Z2M property |
|---|---|
| 31 | `zone_1_total_water` |
| 32 | `zone_2_total_water` |
| 33 | `zone_3_total_water` |
| 34 | `zone_4_total_water` |
| 35 | `zone_5_total_water` |
| 20 | `active_zone` |
| 21 | `max_charging_power_today` |
| 22 | `daily_solar_generation` |
| 23 | `daily_power_consumption` |
| 24 | `battery_voltage` |
| 25 | `pv_voltage` |
| 26 | `pv_power` |
| 27 | `controller_temperature` |
| 28 | `water_level` |
| 41 | `fault_code` |
| 42 | `charging_status` |
| 43 | `waterer_state` |

EP43 also carries the `clear_fault` On/Off command endpoint. Power
Configuration reports provide `battery` and `battery_voltage`. The fault,
charging, waterer-state, and active-zone endpoints are converted from numeric
Analog Input values to stable text enums.

Analog Output writes on the zone endpoints configure watering durations:

| EP | Z2M property |
|---|---|
| 10 | `duration_zone_1` |
| 11 | `duration_zone_2` |
| 12 | `duration_zone_3` |
| 13 | `duration_zone_4` |
| 14 | `duration_zone_5` |

Each duration is exposed as an HA number entity in seconds. The converter and
firmware both clamp values to `config::pump::MIN_WATERING_DURATION_SEC` through
`config::pump::MAX_WATERING_DURATION_SEC`.

Because the converter uses Zigbee2MQTT multi-endpoint mode, HA writes
`duration_zone_N` but `convertSet` receives the normalized key `duration` plus
endpoint metadata. The converter resolves the endpoint, writes
`genAnalogOutput.presentValue`, and returns the base `duration` state so
Zigbee2MQTT publishes the correctly suffixed property for that endpoint.

The converter also exposes a writable `clear_fault` enum. Its `convertSet`
handler sends `genOnOff.on` to endpoint 43. The firmware handles that as an
explicit clear-fault command and resets endpoint 43 back to Off, so repeated
button presses always create a new On transition.

Water totals are exposed on separate Analog Input endpoints instead of the zone
control endpoints. The converter maps EP31-35 to `zone_N_total_water`, reports
the values in liters, and overrides Home Assistant discovery with
`device_class: water` and `state_class: total_increasing` so HA can derive daily
usage with `utility_meter`.

Home Assistant discovery names for the power and energy sensors are overridden
in `overrideHaDiscoveryPayload` so HA displays `PV power`, `Max charging power
today`, `Daily solar generation`, and `Daily power consumption` instead of
generic `Power` / `Energy` labels.

---

## Home Assistant Integration

After the external converter is loaded and the device interview completes, Home
Assistant discovers the device through the MQTT integration. Rename the
Zigbee2MQTT friendly name if you want stable, friendly entity IDs; otherwise HA
will use the IEEE address in the generated entity IDs.

### Exposed Entities

| Entity type | Purpose |
|---|---|
| `switch.zone_1` ... `switch.zone_5` | Manual watering controls |
| `number.duration_zone_1` ... `number.duration_zone_5` | Duration used by the next On command for each zone |
| `button.clear_fault` | Acknowledge and clear a latched fault |
| `sensor.zone_1_total_water` ... `sensor.zone_5_total_water` | Per-zone lifetime water total (L) |
| `sensor.battery` | Renogy battery SOC (%) |
| `sensor.battery_voltage` | Renogy battery voltage (V) |
| `sensor.pv_voltage` | Solar input voltage (V) |
| `sensor.pv_power` | Solar input power (W) |
| `sensor.controller_temperature` | Controller temperature (deg C) |
| `sensor.water_level` | Reservoir fill level (%) |
| `sensor.max_charging_power_today` | Renogy daily max charge power (W) |
| `sensor.daily_solar_generation` | Renogy daily generation (Wh) |
| `sensor.daily_power_consumption` | Renogy daily consumption (Wh) |
| `sensor.fault_code` | Text fault enum |
| `sensor.charging_status` | Text charging-stage enum |
| `sensor.waterer_state` | Aggregate controller state: `idle`, `priming`, `watering`, or `fault` |
| `sensor.active_zone` | FSM-owned zone: `none` or `zone_1` through `zone_5` |

### Manual Watering

```yaml
service: switch.turn_on
target:
  entity_id: switch.zone_1
```

For automations, use `sensor.waterer_state` as the HA-side busy guard:

```yaml
condition:
  - condition: state
    entity_id: sensor.0x1051dbfffe0d375c_waterer_state
    state: idle
```

`active_zone` reports the zone currently being primed/watered, or the faulted
zone when the fault happened after a valve opened. Precheck faults such as low
water report `waterer_state=fault` and `active_zone=none`.

The firmware owns the duration and self-termination. Turn the same switch off to
abort an active run and clear any latched fault:

```yaml
service: switch.turn_off
target:
  entity_id: switch.zone_1
```

To customize a zone duration before watering:

```yaml
service: number.set_value
target:
  entity_id: number.0x1051dbfffe0d375c_duration_zone_1
data:
  value: 45
```

### Daily Water Usage

The FSM records the flow-meter total for every accepted cycle when it completes,
is cancelled, or faults after opening a zone. Precheck faults do not create a
delivery record because no zone owned the pump/valve path yet.

`wateringTask` adds each non-zero delivery to `WaterUsageTracker`, stores the
per-zone milliliter total in the `water_usage` NVS namespace, and reports the
new lifetime total through the zone endpoint's Analog Input cluster as liters.
NVS is written once per completed delivery, not per pulse.

The firmware does not perform daily resets. Home Assistant should derive daily
usage from the monotonic total sensors:

```yaml
utility_meter:
  plant_waterer_zone_1_daily_water:
    source: sensor.0x1051dbfffe0d375c_zone_1_total_water
    cycle: daily
```

### Emergency Stop All

```yaml
type: button
name: "Stop All"
icon: mdi:stop-circle
icon_color: red
tap_action:
  action: call-service
  service: homeassistant.turn_off
  target:
    entity_id:
      - switch.zone_1
      - switch.zone_2
      - switch.zone_3
      - switch.zone_4
      - switch.zone_5
```

### Fault Notification and Clear

This pattern reports a fault and then presses the converter-provided clear
button. Adjust the entity IDs to match the friendly name in your installation.

```yaml
alias: Plant waterer fault notify and clear
mode: queued
trigger:
  - platform: state
    entity_id: sensor.0x1051dbfffe0d375c_fault_code
condition:
  - condition: template
    value_template: "{{ trigger.to_state.state not in ['none', 'unknown', 'unavailable'] }}"
action:
  - service: notify.mobile_app_phone
    data:
      title: Plant waterer fault
      message: "{{ trigger.to_state.state }}"
  - service: button.press
    target:
      entity_id: button.0x1051dbfffe0d375c_clear_fault
```

### Firmware Logs

The serial log intentionally mirrors the HA-visible state so field debugging is
possible without a packet sniffer:

```text
Zone 1 duration set to 45 s
Zone 1 ON command received (45 s)
Zone 1 watering started (45 s)
Zone 2 ON command received (15 s)
Zone 2 request ignored: watering already in progress
Zone 2 switch forced off because another zone is active
Zone 1 request rejected: water_low (2)
Zone 1 switch forced off after rejected request
Zone 1 OFF command received
Fault clear requested via Zone 1 OFF: water_low (2)
Zone 1 status: idle -> priming
Zone 1 status: priming -> running
Zone 1 status: running -> idle
Zone 1 dispensed 71 mL (lifetime 1042 mL / 1.042 L)
Stopping pump
Waiting 100 ms before closing solenoid
Closing all solenoids
Waiting 4500 ms before disabling Renogy load
Disabling Renogy load
Waterer state: priming
Active zone: 1
Waterer state: idle
Active zone: 0
Fault raised: prime_timeout (3)
Clear fault command received
Fault clear requested: prime_timeout (3)
Fault cleared
Water level: 100% (315 mV)
Renogy poll OK: SOC 100% 13.3V PV 0.0V 0W maxChg=0W gen=0Wh con=0Wh status=0
```

Water-level logging occurs on the same cadence as the water-level Zigbee report,
currently `config::renogy::POLL_INTERVAL_MS` (30 seconds).

---

## Build Order / Development Sequence

Work in this order so each layer is testable before the next:

1. **BTS7960 solenoids** — manually trigger one solenoid; verify pull-in/hold current on IS pin with multimeter; confirm hold duty keeps it open without overheating
2. **Pump** — verify RPWM speed control and IS readback; confirm one-way valve holds column
3. **Flow meter** — run water through sensor, verify pulse rate matches F=23.6×Q; confirm mL/pulse against a measured volume
4. **Float sensor** — read ADC at full and empty reservoir; map to 0–100%
5. **Renogy Modbus** — verify register reads match controller display via serial monitor
6. **FSM simulation** — simulate a full watering cycle with `ESP_LOGI` logging and no water; verify state transitions and fault paths
7. **Zigbee** — add last; pair with Z2M, verify entity discovery in HA, test start/abort commands

---

## Notes & Cautions

- **LPWM on pump driver**: Tie to GND permanently — the one-way valve makes reverse impossible and inadvertent reverse drive would stall the pump
- **Never dead-head the pump**: The FSM ensures a solenoid is open before the pump starts. This is a hard invariant — do not add code paths that bypass it
- **LiFePO4 cutoff**: The Renogy Wanderer handles hardware cutoff, but firmware must also gate watering below `MIN_BATTERY_SOC_PCT` to avoid deep discharge during high-load events
- **Zigbee and RS232 coexistence**: Keep RS232 wiring away from the C6 antenna area
- **Conformal coating**: Apply to all driver boards — water and electronics are in close proximity
- **NVS storage**: Per-zone lifetime water totals are stored in the `water_usage` namespace as milliliters and reported to HA as total-increasing liter sensors
- **OTA**: ESP-IDF supports OTA updates over WiFi — not applicable here (no WiFi), but Zigbee OTA cluster (cluster 0x0019) is available if desired for future field updates
