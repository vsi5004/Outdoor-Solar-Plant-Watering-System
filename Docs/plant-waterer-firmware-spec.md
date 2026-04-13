# Solar Balcony Plant Watering System — Firmware Specification

## Overview

An ESP32-C6 WROOM coordinates a solar-powered, multi-zone balcony plant watering system. It communicates with Home Assistant via Zigbee and manages a pump, five solenoid valves, a flow meter, water level sensor, and a Renogy solar charge controller over Modbus RTU.

The firmware owns all timing and safety logic. Home Assistant sends a start command and a duration — the firmware executes and self-terminates. No watering cycle timing depends on wireless connectivity.

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

## Pin Assignment (config.h)

All hardware mappings defined in one place. Based on the ESP32-C6-DevKitC-1 pinout.

**Pins reserved / avoided:**
- GPIO8 — onboard RGB LED + boot strapping pin
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
| GPIO8 | RGB LED | Reserved — boot strapping pin |
| GPIO9 | — | Reserved — boot strapping pin |
| GPIO12 | USB D- | Reserved |
| GPIO13 | USB D+ | Reserved |

---

## Watering Constants (config.h)

```c
// Solenoid PWM behavior
#define SOL_PULL_IN_MS          120       // Full duty duration on open
#define SOL_HOLD_DUTY_PCT       38        // Hold duty after pull-in (~30-40% typical)
#define SOL_HOLD_CURRENT_MA     180       // Expected solenoid 5 hold current (calibrate)

// Pump
#define PUMP_PRIME_TIMEOUT_MS   15000     // Max time to wait for flow pulses
#define PUMP_PRIME_PULSE_COUNT  5         // Pulses before considered primed
#define PUMP_DRY_RUN_MA         200       // Below this while running = dry run fault
#define MAX_DISPENSE_MS         (30 * 60 * 1000)  // 30 min hard cap

// Safety thresholds
#define MIN_BATTERY_SOC_PCT     15
#define MIN_WATER_LEVEL_PCT     10

// Flow calibration — derived from datasheet F = 23.6 × Q (Hz, L/min)
// mL/pulse = 1000 / (60 × 23.6) ≈ 0.706
constexpr float ML_PER_PULSE = 0.706f;

// Renogy polling
#define RENOGY_POLL_INTERVAL_MS 30000
```

---

## Project Structure

```
plant-waterer/
├── main/
│   ├── main.c                  # App entry, task and queue creation
│   ├── config.h                # All pin defs, constants, thresholds
│   ├── drivers/
│   │   ├── bts7960.h/.c        # PWM control + IS current readback
│   │   ├── flow_meter.h/.c     # PCNT pulse counter + ml calculation
│   │   ├── float_sensor.h/.c   # ADC water level → percentage
│   │   └── renogy.h/.c         # Modbus RTU UART driver
│   ├── watering/
│   │   ├── zone.h/.c           # Per-zone config, open/close, solenoid mapping
│   │   ├── scheduler.h/.c      # Time-based schedule → posts to water_queue
│   │   └── fsm.h/.c            # Watering state machine task
│   └── zigbee/
│       ├── zb_device.h/.c      # Cluster/endpoint registration, attribute updates
│       └── zb_handlers.h/.c    # Command callbacks from HA
├── CMakeLists.txt
└── sdkconfig
```

---

## Driver: BTS7960 (bts7960.h/.c)

### Key Behaviors

**Pump (left half of board #1):**
- LPWM permanently tied LOW (one-way valve — no reverse)
- RPWM controls speed via LEDC PWM (0–100%)
- R_EN high to enable, low to coast-stop

**Solenoids (right half of board #1, both halves of boards #2 and #3):**
- Each half-bridge drives one solenoid: output pin to solenoid+, solenoid- to GND
- Pull-in phase: 100% duty for `SOL_PULL_IN_MS` milliseconds
- Hold phase: `SOL_HOLD_DUTY_PCT`% duty — reduces coil heat, maintains open state
- Caller invokes `bts7960_solenoid_pulse()` to open; must call `bts7960_solenoid_off()` to close

**LEDC Configuration:**
- Timer: ~1kHz, 10-bit resolution (0–1023)
- Each board needs up to 2 LEDC channels (one per active half)
- Use `LEDC_LOW_SPEED_MODE`

**IS Pin (current sense):**
- Single IS pin shared across both half-bridges per chip
- Output current = I_load / 8500 (approximate, verify datasheet revision)
- With 1kΩ sense resistor to GND: V_IS = I_load / 8500 * 1000
- Therefore: I_load_mA = V_IS * 8500
- Read via `adc_oneshot` API

**Solenoid 5 / Pump IS correction:**
Boards #1 shares IS between pump (left) and solenoid 5 (right). Since they are active simultaneously during zone 5 watering:

```c
float read_pump_current_ma(void) {
    float total = bts7960_read_current_ma(&drv1);
    if (zone_is_open(5)) {
        total -= SOL_HOLD_CURRENT_MA;  // Subtract known solenoid 5 hold current
    }
    return total;
}
```

Dry-run detection threshold (~1–2A) is well above solenoid noise so the correction margin is comfortable.

### Data Structure

```c
typedef struct {
    int rpwm_pin, lpwm_pin;
    int r_en_pin, l_en_pin;
    int is_adc_channel;
    ledc_channel_t r_channel;
    ledc_channel_t l_channel;
} bts7960_t;
```

### Functions to Implement

```c
void  bts7960_init(bts7960_t *dev);
void  bts7960_pump_set(bts7960_t *dev, uint8_t duty_pct);
void  bts7960_solenoid_pulse(bts7960_t *dev, bool use_right_half);
void  bts7960_solenoid_off(bts7960_t *dev, bool use_right_half);
float bts7960_read_current_ma(bts7960_t *dev);
```

---

## Driver: Flow Meter (flow_meter.hpp/.cpp)

Uses the ESP-IDF **PCNT** (Pulse Counter) peripheral — hardware accelerated, zero CPU overhead.
Implemented as `EspPcnt` (HAL) + `FlowMeter` (driver).

- Counts rising edges on `FLOW_METER_PIN` (GPIO6)
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
| Empty | 1361 mV    |
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

## Zone Mapping (zone.h/.c)

Map zone numbers 1–5 to their driver board and half-bridge side.

```c
typedef struct {
    bts7960_t *driver;
    bool       use_right_half;   // false = left (LPWM), true = right (RPWM)
    bool       is_open;
    char       name[16];
} zone_t;
```

Zone table:

| Zone | Driver | Half | Notes |
|---|---|---|---|
| 1 | drv2 | Left | LPWM |
| 2 | drv2 | Right | RPWM |
| 3 | drv3 | Left | LPWM |
| 4 | drv3 | Right | RPWM |
| 5 | drv1 | Right | RPWM — shares IS with pump |

**Functions:**
```c
void zone_open(uint8_t zone_num);    // pull-in then hold
void zone_close(uint8_t zone_num);
void zone_close_all(void);
bool zone_is_open(uint8_t zone_num);
```

---

## Watering State Machine (fsm.h/.c)

Runs in its own FreeRTOS task at priority 5, 10Hz tick rate.

### States

```c
typedef enum {
    WS_IDLE,
    WS_PRECHECK,         // Verify battery SOC and water level
    WS_OPEN_SOLENOID,    // Open target zone valve
    WS_START_PUMP,       // Enable pump, reset flow counter
    WS_PRIMING,          // Wait for flow pulses (air purge)
    WS_DISPENSING,       // Track volume + current, self-terminate on duration
    WS_STOP_PUMP,        // Halt pump, pressure equalize delay
    WS_CLOSE_SOLENOID,   // Close valve, report completion to Zigbee
    WS_FAULT,            // Emergency stop all, report fault to HA
} watering_state_t;
```

### Context

```c
typedef struct {
    watering_state_t state;
    uint8_t          zone;
    uint32_t         duration_sec;        // Set at start — never changes mid-cycle
    uint32_t         dispense_start_ms;
    uint32_t         ml_dispensed;
    watering_source_t source;             // HA_MANUAL, HA_SCHEDULE, LOCAL_SCHEDULE
} watering_ctx_t;
```

### Request Queue

```c
typedef struct {
    uint8_t  zone;
    uint32_t duration_sec;
    watering_source_t source;
} watering_request_t;

extern QueueHandle_t water_queue;  // xQueueCreate(5, sizeof(watering_request_t))
```

### State Transition Logic

**WS_IDLE:**
- Poll `water_queue` (non-blocking)
- On receipt → populate ctx, transition to `WS_PRECHECK`

**WS_PRECHECK:**
- Read latest `renogy_data_t` (mutex-protected)
- If `battery_soc < MIN_BATTERY_SOC_PCT` → `FAULT_LOW_BATTERY`
- If `float_sensor_get_pct() < MIN_WATER_LEVEL_PCT` → `FAULT_LOW_WATER`
- If `duration_sec == 0` → `FAULT_INVALID_REQUEST`
- Pass → `WS_OPEN_SOLENOID`

**WS_OPEN_SOLENOID:**
- Call `zone_open(ctx.zone)` (triggers pull-in → hold sequence inside driver)
- Wait 200ms for solenoid to fully open
- → `WS_START_PUMP`

**WS_START_PUMP:**
- `flow_meter_reset()`
- `ctx.dispense_start_ms = now`
- `bts7960_pump_set(&drv1_pump, 100)`
- `zb_set_zone_status(ctx.zone, ZONE_STATUS_PRIMING)`
- → `WS_PRIMING`

**WS_PRIMING:**
- If `flow_meter_get_pulses() > PUMP_PRIME_PULSE_COUNT` → `WS_DISPENSING`
- If elapsed > `PUMP_PRIME_TIMEOUT_MS` → `FAULT_PRIME_TIMEOUT` → `WS_STOP_PUMP`
- Optionally cross-reference IS current: low & noisy = air, higher & steadier = water

**WS_DISPENSING:**
- Update `ctx.ml_dispensed = flow_meter_get_ml()`
- If elapsed >= `ctx.duration_sec * 1000` → `WS_STOP_PUMP` ← **primary termination**
- If elapsed >= `MAX_DISPENSE_MS` → `FAULT_MAX_DURATION` → `WS_STOP_PUMP` ← **hard cap**
- If `read_pump_current_ma() < PUMP_DRY_RUN_MA` → `FAULT_DRY_RUN` → `WS_STOP_PUMP`
- Push live progress: `zb_set_zone_ml_dispensed(ctx.zone, ctx.ml_dispensed)`

**WS_STOP_PUMP:**
- `bts7960_pump_set(&drv1_pump, 0)`
- Delay 500ms (pressure equalization)
- → `WS_CLOSE_SOLENOID`

**WS_CLOSE_SOLENOID:**
- `zone_close(ctx.zone)`
- `zb_set_zone_status(ctx.zone, ZONE_STATUS_IDLE)`
- `zb_set_zone_state(ctx.zone, false)` ← sync Zigbee On/Off attribute back to OFF
- `zb_report_zone_complete(ctx.zone, ctx.ml_dispensed)`
- → `WS_IDLE`

**WS_FAULT:**
- `gpio_set_level(DRV_MASTER_EN_PIN, 0)` ← cuts all boards instantly, no PWM sequencing needed
- `zone_close_all()` ← also zero out all PWM duty registers for clean re-enable later
- `zb_set_zone_status(ctx.zone, ZONE_STATUS_FAULT)`
- `zb_report_fault(active_fault)`
- → `WS_IDLE` (or latch until cleared by HA command)

### Safety Interlock (enforced in firmware regardless of HA state)
- Pump must NEVER run with all solenoids closed (dead-head risk)
- Pump is only started from `WS_START_PUMP` which always follows `WS_OPEN_SOLENOID`
- On any fault → pump stops before or simultaneously with solenoid close

---

## Fault Codes

```cpp
enum class FaultCode : uint8_t {
    None             = 0,
    LowBattery       = 1,  // Battery SOC below MIN_BATTERY_SOC_PCT
    LowWater         = 2,  // Reservoir below MIN_WATER_LEVEL_PCT
    PrimeTimeout     = 3,  // No flow pulses within PUMP_PRIME_TIMEOUT_MS
    DryRun           = 4,  // Pump IS current below DRY_RUN_MA while dispensing
    MaxDuration      = 5,  // Watering exceeded MAX_DISPENSE_MS hard cap
    InvalidRequest   = 6,  // duration_sec == 0 or zone out of range
    LoadEnableFailed = 7,  // Renogy setLoad(true) returned false before pump start
};
```

Values are stable — do not reorder. HA automations may key off the integer value
reported on EP 41.

---

## Task Architecture (main.c)

```c
void app_main(void) {
    // Init order matters
    adc_init();                 // Shared ADC1 handle
    flow_meter_init();
    bts7960_init_all();         // Init all three driver structs + LEDC
    renogy_uart_init();
    zigbee_init();              // Starts Zigbee stack task internally

    water_queue = xQueueCreate(5, sizeof(watering_request_t));

    xTaskCreate(watering_task,  "watering", 4096, NULL, 5, NULL);
    xTaskCreate(renogy_task,    "renogy",   2048, NULL, 2, NULL);
    xTaskCreate(adc_task,       "adc",      2048, NULL, 3, NULL);
    xTaskCreate(scheduler_task, "sched",    2048, NULL, 2, NULL);
}
```

| Task | Priority | Rate | Responsibility |
|---|---|---|---|
| `watering_task` | 5 | 10Hz FSM tick | Pump, solenoid, fault handling |
| `adc_task` | 3 | 4Hz | IS current readback, float sensor |
| `renogy_task` | 2 | 1/30s | Modbus poll, update solar struct |
| `scheduler_task` | 2 | 1/min | Time-based schedules → water_queue |
| Zigbee (internal) | — | Event-driven | HA communication |

**Shared data access:**
- `renogy_data_t` protected by `SemaphoreHandle_t renogy_mutex`
- `adc_task` writes IS currents and float level to atomic globals read by FSM and Zigbee tasks
- Zigbee attribute writes use `esp_zb_lock_acquire` / `esp_zb_lock_release`

---

## Zigbee Interface (zb_device.h/.c + zb_handlers.h/.c)

### Design Principle

HA sends: `duration_seconds` (write to Analog Output attribute) + `On` command (to On/Off cluster).
Firmware runs the cycle and self-terminates.
HA receives: live status updates via attribute reporting.
HA sends `Off` command only to abort an in-progress cycle.

### Endpoint Layout

| EP | Cluster | Role | Content |
|---|---|---|---|
| 1 | Basic + Identify | Server | Mandatory Zigbee device descriptors |
| 10 | On/Off | Server | Zone 1 active state (on = Priming or Running) |
| 11 | On/Off | Server | Zone 2 active state |
| 12 | On/Off | Server | Zone 3 active state |
| 13 | On/Off | Server | Zone 4 active state |
| 14 | On/Off | Server | Zone 5 active state |
| 20 | Power Configuration | Server | `BatteryPercentageRemaining` (SOC × 2), `BatteryVoltage` (V × 10) |
| 21 | Analog Input | Server | Max charging power today (W) |
| 22 | Analog Input | Server | Daily solar generation (Wh) |
| 23 | Analog Input | Server | Daily power consumption (Wh) |
| 41 | Multistate Input | Server | `FaultCode` (8 states, 0 = None) |
| 42 | Multistate Input | Server | Charging status (7 states: 0=not started … 5=float … 6=current limiting) |

All Renogy data comes from the background `renogy_task` polling at 30s intervals.
Zone on/off states are pushed from the `watering_task` on every state change.

### Status Enum

```c
typedef enum {
    ZONE_STATUS_IDLE    = 0,
    ZONE_STATUS_PRIMING = 1,
    ZONE_STATUS_RUNNING = 2,
    ZONE_STATUS_FAULT   = 3,
} zone_status_t;
```

### Command Handler (zb_handlers.c)

```c
esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t cb_id,
                             const void *msg) {
    if (cb_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        const esp_zb_zcl_set_attr_value_message_t *m = msg;
        uint8_t ep = m->info.dst_endpoint;

        if (m->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF && ep >= 1 && ep <= 5) {
            bool on = *(bool *)m->attribute.data.value;
            if (on) {
                uint16_t duration_sec = zb_get_zone_duration(ep);
                if (duration_sec == 0) {
                    zb_set_zone_status(ep, ZONE_STATUS_FAULT);
                    return ESP_OK;
                }
                watering_request_t req = {
                    .zone = ep,
                    .duration_sec = duration_sec,
                    .source = WATER_SRC_HA_MANUAL,
                };
                xQueueSend(water_queue, &req, 0);
            } else {
                watering_abort_zone(ep);
            }
        }
    }

    if (cb_id == ESP_ZB_CORE_NETWORK_JOIN_CB_ID) {
        configure_reporting();   // Set up attribute reporting after join
    }

    return ESP_OK;
}
```

### Public Update Functions

```c
void zb_set_zone_state(uint8_t zone, bool on);
void zb_set_zone_status(uint8_t zone, zone_status_t status);
void zb_set_zone_ml_dispensed(uint8_t zone, uint32_t ml);
void zb_set_analog(uint8_t endpoint, float value);
void zb_set_battery_soc(uint8_t percent);
void zb_report_fault(fault_code_t fault);
uint16_t zb_get_zone_duration(uint8_t zone_ep);
```

### Attribute Reporting Configuration

Call `configure_reporting()` after network join. Configure each sensor endpoint with:
- `min_interval`: 5s (no faster)
- `max_interval`: 60s (force periodic report)
- Report on change (delta appropriate to each sensor)

Zone status and ml_dispensed should report on every change during an active cycle.

### Zigbee Device Config

```c
esp_zb_cfg_t zb_cfg = {
    .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,   // End device (not router)
    .install_code_policy = false,
};
#define ZB_PRIMARY_CHANNEL_MASK (1l << 15)   // Match your coordinator channel
```

Model string (used by Z2M for custom converter matching):
```c
// Set during endpoint registration
// Model: "PlantWaterer"
// Manufacturer: "DIY"
```

---

## Zigbee2MQTT Custom Converter (plant_waterer.js)

Place in Z2M `data/external_converters/` directory.

```javascript
const {onOff, numeric} = require('zigbee-herdsman-converters/lib/modernExtend');

const definition = {
    zigbeeModel: ['PlantWaterer'],
    model: 'PLANT-WATERER-1',
    vendor: 'DIY',
    description: 'Solar balcony plant watering system',
    extend: [
        onOff({ endpointNames: ['zone_1','zone_2','zone_3','zone_4','zone_5','pump'] }),

        // Zone duration (writable) + status + volume per zone
        ...[1,2,3,4,5].flatMap(z => [
            numeric({
                name: `zone_${z}_duration`,
                cluster: 'genAnalogOutput',
                attribute: 'presentValue',
                unit: 's', min: 10, max: 1800, step: 10,
                endpoint: `zone_${z}`,
                access: 'ALL',
            }),
            numeric({
                name: `zone_${z}_status`,
                cluster: 'genAnalogInput',
                attribute: 'presentValue',
                description: '0=idle 1=priming 2=running 3=fault',
                endpoint: `zone_${z}_status`,
                access: 'STATE',
            }),
            numeric({
                name: `zone_${z}_ml_dispensed`,
                cluster: 'genAnalogInput',
                attribute: 'presentValue',
                unit: 'mL',
                endpoint: `zone_${z}_vol`,
                access: 'STATE',
            }),
        ]),

        numeric({ name: 'water_level',    cluster: 'genAnalogInput', attribute: 'presentValue', unit: '%',     endpoint: 'water_level', access: 'STATE', device_class: 'moisture' }),
        numeric({ name: 'flow_rate',      cluster: 'genAnalogInput', attribute: 'presentValue', unit: 'mL/min',endpoint: 'flow_rate',   access: 'STATE' }),
        numeric({ name: 'pv_voltage',     cluster: 'genAnalogInput', attribute: 'presentValue', unit: 'V',     endpoint: 'pv_volt',     access: 'STATE', device_class: 'voltage' }),
        numeric({ name: 'pv_power',       cluster: 'genAnalogInput', attribute: 'presentValue', unit: 'W',     endpoint: 'pv_power',    access: 'STATE', device_class: 'power' }),
        numeric({ name: 'battery_voltage',cluster: 'genAnalogInput', attribute: 'presentValue', unit: 'V',     endpoint: 'batt_volt',   access: 'STATE', device_class: 'voltage' }),
        numeric({ name: 'pump_current',   cluster: 'genAnalogInput', attribute: 'presentValue', unit: 'mA',    endpoint: 'pump_curr',   access: 'STATE', device_class: 'current' }),
        numeric({ name: 'fault_code',     cluster: 'genAnalogInput', attribute: 'presentValue',               endpoint: 'faults',      access: 'STATE' }),
    ],

    endpoint: (device) => ({
        zone_1: 1, zone_2: 2, zone_3: 3, zone_4: 4, zone_5: 5,
        pump: 6,
        zone_1_status: 10, zone_2_status: 11, zone_3_status: 12,
        zone_4_status: 13, zone_5_status: 14,
        zone_1_vol: 15, zone_2_vol: 16, zone_3_vol: 17,
        zone_4_vol: 18, zone_5_vol: 19,
        water_level: 20, flow_rate: 21,
        batt_soc: 30, batt_volt: 31, pv_volt: 32, pv_power: 33,
        pump_curr: 40, faults: 41,
    }),
    meta: { multiEndpoint: true },
};

module.exports = definition;
```

---

## Home Assistant Integration

### Manual Watering Script

```yaml
script:
  water_zone:
    alias: "Water Zone"
    fields:
      zone:
        description: "Zone number 1-5"
        example: 1
      duration_seconds:
        description: "Duration in seconds"
        example: 120
    sequence:
      - service: number.set_value
        target:
          entity_id: "number.zone_{{ zone }}_duration"
        data:
          value: "{{ duration_seconds }}"
      - delay:
          milliseconds: 200
      - service: switch.turn_on
        target:
          entity_id: "switch.zone_{{ zone }}"
      # Script exits here — firmware owns the rest
```

### Dashboard Card (per zone)

```yaml
type: horizontal-stack
cards:
  - type: entity
    entity: number.zone_1_duration
    name: "Zone 1 (s)"
  - type: button
    name: "Water"
    icon: mdi:watering-can
    tap_action:
      action: call-service
      service: script.water_zone
      service_data:
        zone: 1
        duration_seconds: "{{ states('number.zone_1_duration') | int }}"
  - type: entity
    entity: sensor.zone_1_status
    name: "Status"
  - type: entity
    entity: sensor.zone_1_ml_dispensed
    name: "mL"
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

---

## Build Order / Development Sequence

Work in this order so each layer is testable before the next:

1. **BTS7960 solenoids** — manually trigger one solenoid; verify pull-in/hold current on IS pin with multimeter; confirm hold duty keeps it open without overheating
2. **Pump** — verify RPWM speed control and IS readback; confirm one-way valve holds column
3. **Flow meter** — run water through sensor, verify pulse rate matches F=23.6×Q; confirm mL/pulse against a measured volume
4. **Float sensor** — read ADC at full and empty reservoir; map to 0–100%
5. **Renogy Modbus** — verify register reads match controller display via serial monitor
6. **FSM dry run** — simulate a full watering cycle with `ESP_LOGI` logging and no water; verify state transitions and fault paths
7. **Zigbee** — add last; pair with Z2M, verify entity discovery in HA, test start/abort commands

---

## Notes & Cautions

- **LPWM on pump driver**: Tie to GND permanently — the one-way valve makes reverse impossible and inadvertent reverse drive would stall the pump
- **Never dead-head the pump**: The FSM ensures a solenoid is open before the pump starts. This is a hard invariant — do not add code paths that bypass it
- **LiFePO4 cutoff**: The Renogy Wanderer handles hardware cutoff, but firmware must also gate watering below `MIN_BATTERY_SOC_PCT` to avoid deep discharge during high-load events
- **Zigbee and RS232 coexistence**: Keep RS232 wiring away from the C6 antenna area
- **Conformal coating**: Apply to all driver boards — water and electronics are in close proximity
- **NVS storage**: Consider storing per-zone total ml dispensed (lifetime) and last-run timestamp in NVS for persistence across reboots and reporting to HA
- **OTA**: ESP-IDF supports OTA updates over WiFi — not applicable here (no WiFi), but Zigbee OTA cluster (cluster 0x0019) is available if desired for future field updates
