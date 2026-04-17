# Solar Balcony Plant Watering System — Firmware

ESP32-C6 firmware for a solar-powered, 5-zone balcony plant watering system.
Communicates with Home Assistant via Zigbee (router) and manages a pump,
five solenoid valves, a flow meter, a water level sensor, and a Renogy solar
charge controller over Modbus RTU.

See [`Docs/plant-waterer-firmware-spec.md`](Docs/plant-waterer-firmware-spec.md)
for full hardware and protocol details.

---

## Prerequisites

### ESP-IDF v5.4

Install **ESP-IDF v5.4.1** and ensure `idf.py` is on your PATH.

- [Getting Started — ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32c6/get-started/)
- Recommended: use the VS Code ESP-IDF extension installer (see VS Code Setup below)

> **Windows note:** ESP-IDF does **not** support Git Bash or MSYS2 terminals.
> All `idf.py` commands must be run from the **VS Code integrated terminal**
> (after configuring the ESP-IDF extension) or the **ESP-IDF CMD/PowerShell**
> shortcut created by the installer. Use Docker for host tests if you do not
> want to configure a native shell.

### VS Code

Install [VS Code](https://code.visualstudio.com/) and open this folder.
Accept the recommended extensions prompt (`.vscode/extensions.json`), or install
**Espressif IDF** manually from the marketplace.

Run **ESP-IDF: Configure ESP-IDF Extension** from the command palette and point
it at your v5.4.1 install. Then update `idf.port` in `.vscode/settings.json`
to match your serial port (e.g. `COM3`).

### Docker (recommended for host tests on Windows)

Install [Docker Desktop](https://www.docker.com/get-started/).
The `espressif/idf:v5.4.1` image is pulled automatically on first run.
No local ESP-IDF install required.

---

## Building and Flashing the Firmware

From the **VS Code integrated terminal** (or ESP-IDF CMD):

```bash
# One-time: set the target chip
idf.py set-target esp32c6

# Build
idf.py build

# Flash and open serial monitor
idf.py -p COM3 flash monitor
```

From VS Code: **Terminal → Run Task → Firmware: Flash + Monitor**.

On this Windows workstation, the ESP-IDF install used during bring-up is:

```powershell
$env:IDF_TOOLS_PATH='C:\Users\isirc\.espressif'
. 'C:\Users\isirc\esp\v5.4\esp-idf\export.ps1'
idf.py build
```

The project uses the custom partition table in [`partitions.csv`](partitions.csv).
It keeps the factory app at `0xf0000` bytes and adds `zb_storage` / `zb_fct`
FAT partitions required by the ESP Zigbee stack.

---

## Testing

This project uses [Unity](https://www.throwtheswitch.org/unity) via the ESP-IDF
test framework. Tests are split into two categories.

### Host-based tests (no hardware required)

Covers pure-logic modules: driver math, FSM state transitions, Modbus CRC/parsing.
Runs on your PC via Docker and in GitHub Actions CI.

**Option A — Docker (recommended on Windows):**

```bash
bash scripts/run_host_tests.sh
```

Expected output ends with:
```
X Tests 0 Failures 0 Ignored
OK
```

**Option B — Native** (VS Code integrated terminal or ESP-IDF CMD — not Git Bash):

```bash
cd test/host
idf.py --preview set-target linux
idf.py build
./build/test_host.elf
```

From VS Code: **Terminal → Run Task → Host Tests: Build + Run (Docker)**
or **Host Tests: Run (native)**.

### Target-based tests (HIL — hardware required)

Covers hardware peripheral drivers: PCNT flow meter, LEDC PWM, ADC oneshot,
UART Modbus. Run these on a connected ESP32-C6 before each deployment.

```bash
cd test/target
idf.py set-target esp32c6
idf.py -p COM3 flash monitor
```

Once the device boots you will see a test menu. Press `Enter` to see options,
or type `a` + `Enter` to run all tests immediately.

From VS Code: **Terminal → Run Task → Target Tests: Flash + Monitor**.

#### Adding target tests

`TEST_CASE` registrations are placed in linker sections by the preprocessor.
Because the linker only pulls in object files that contain a directly referenced
symbol, any test file whose only content is `TEST_CASE` macros will be silently
dropped unless something forces it to link. The fix is a trivial registration
function:

1. Add `void register_<module>_tests() {}` at the top of `test_<module>.cpp`.
2. Declare and call it from `app_main` in `test_runner.cpp`:

```cpp
extern void register_float_sensor_tests();

extern "C" void app_main(void) {
    register_float_sensor_tests();
    unity_run_menu();
}
```

3. Add the `.cpp` file and any required driver sources to `SRCS` in
   `test/target/main/CMakeLists.txt` using `${CMAKE_CURRENT_LIST_DIR}`-relative
   absolute paths.

---

## Adding New Tests

All host tests live in `test/host/main/`. The pattern is:

1. Create `test/host/main/test_<module>.cpp` with static test functions and a
   `run_<module>_tests()` function that calls `RUN_TEST(fn)` for each:

```cpp
// test/host/main/test_my_module.cpp
#include "unity.h"
#include "drivers/my_module.hpp"

static void test_something(void) {
    TEST_ASSERT_EQUAL(42, my_module_compute(6, 7));
}

void run_my_module_tests(void) {
    RUN_TEST(test_something);
}
```

2. Add a forward declaration and a `run_my_module_tests()` call to `test_runner.cpp`.

3. Add the new `.cpp` file (and any driver `.cpp` files under test) to `SRCS` in
   `test/host/main/CMakeLists.txt`.

> **Why `RUN_TEST` and not `TEST_CASE`?**  
> ESP-IDF's `TEST_CASE` macro relies on linker-section registration that is
> silently dropped when object files are in a static library on the Linux target.
> Explicit `RUN_TEST` calls are portable and guaranteed to work on both targets.
> `TEST_CASE` tag-based filtering is still used in `test/target/` where the
> interactive `unity_run_menu()` runner is appropriate.

---

## CI

GitHub Actions runs all host-based tests on every push and pull request to `main`
using the `espressif/idf:v5.4.1` Docker image.
See [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

Target-based (HIL) tests require physical hardware and are run manually before
deployment. They are not part of CI.

---

## Endpoint Layout (Zigbee)

| EP | Cluster | Content |
|---|---|---|
| 1 | Basic + Identify | Mandatory device descriptors |
| 10–14 | On/Off + Analog Output | Zone 1–5 active state + per-zone watering duration in seconds |
| 20 | Power Configuration + Analog Input | Battery SOC/voltage + active zone (`0.0`=none, `1.0`–`5.0`=zone number) |
| 21 | Analog Input | Max charging power today (W) |
| 22 | Analog Input | Daily solar generation (Wh) |
| 23 | Analog Input | Daily power consumption (Wh) |
| 24 | Analog Input | Battery voltage (V) |
| 25 | Analog Input | PV voltage (V) |
| 26 | Analog Input | PV power (W) |
| 27 | Analog Input | Controller temperature (deg C) |
| 28 | Analog Input | Reservoir water level (%) |
| 41 | Analog Input | Fault code (`0.0`–`7.0`; see `fault_code.hpp`) |
| 42 | Analog Input | Charging status (`0.0`=off, `2.0`=MPPT, `5.0`=float, etc.) |
| 43 | On/Off + Analog Input | Momentary clear-fault command + waterer state (`0.0`=idle, `1.0`=priming, `2.0`=watering, `3.0`=fault) |

---

## Zigbee2MQTT and Home Assistant

This device needs the external converter in
[`zigbee2mqtt/solar-plant-waterer.mjs`](zigbee2mqtt/solar-plant-waterer.mjs).
Copy it to the Zigbee2MQTT data directory on the server:

```text
data/external_converters/solar-plant-waterer.mjs
```

Current deployment target:

```text
ivan@jarvis:~/homeassistant/zigbee2mqtt-data/external_converters/solar-plant-waterer.mjs
```

Restart command on that host:

```bash
cd ~/homeassistant
docker compose restart zigbee2mqtt
```

A healthy Zigbee2MQTT startup log contains:

```text
Loaded external converter 'solar-plant-waterer.mjs'.
```

The converter matches the Zigbee Basic cluster values:

| Field | Value |
|---|---|
| Manufacturer | `Ivanbuilds` |
| Model identifier | `solar-plant-waterer` |

### Exposed HA Entities

| Entity | Source |
|---|---|
| `switch.zone_1` ... `switch.zone_5` | On/Off endpoints 10-14 |
| `number.duration_zone_1` ... `number.duration_zone_5` | Analog Output endpoints 10-14, 1-1800 seconds |
| `sensor.battery` | Power Config battery percentage |
| `sensor.battery_voltage` | Power Config + EP24 |
| `sensor.pv_voltage` | EP25 Analog Input |
| `sensor.pv_power` | EP26 Analog Input |
| `sensor.controller_temperature` | EP27 Analog Input |
| `sensor.water_level` | EP28 Analog Input |
| `sensor.max_charging_power_today` | EP21 Analog Input |
| `sensor.daily_solar_generation` | EP22 Analog Input |
| `sensor.daily_power_consumption` | EP23 Analog Input |
| `sensor.fault_code` | EP41 Analog Input, converted to a text enum |
| `sensor.charging_status` | EP42 Analog Input, converted to a text enum |
| `sensor.waterer_state` | EP43 Analog Input, converted to `idle`/`priming`/`watering`/`fault` |
| `sensor.active_zone` | EP20 Analog Input, converted to `none`/`zone_1`...`zone_5` |
| `button.clear_fault` | Z2M converter command, sends clear endpoint 43 `On` |

Zigbee2MQTT removes generic switch power-on behavior entities by using
`m.onOff({powerOnBehavior: false})` in the converter. The converter also
overrides Home Assistant discovery payload names for power and energy sensors so
HA shows `PV power`, `Max charging power today`, `Daily solar generation`, and
`Daily power consumption` instead of generic `Power` / `Energy` labels.

### Fault Codes

Faults are latched by the watering FSM until cleared. The firmware reports the
integer code on endpoint 41 and the converter maps it to a stable text value.

| Code | HA value | Meaning |
|---|---|---|
| 0 | `none` | No active fault |
| 1 | `battery_low` | Renogy SOC is at or below `MIN_BATTERY_SOC_PCT` |
| 2 | `water_low` | Reservoir level is at or below `MIN_WATER_LEVEL_PCT` |
| 3 | `prime_timeout` | Flow pulses did not arrive before `config::pump::PRIME_TIMEOUT_MS` |
| 4 | `max_duration` | Watering exceeded `MAX_DISPENSE_MS` |
| 5 | `invalid_request` | Bad zone or zero duration |
| 6 | `load_enable_failed` | Renogy `setLoad(true)` failed |
| 7 | `stale_data` | Renogy data is absent or older than `STALE_THRESHOLD_MS` |

### Controller State

The firmware reports aggregate state for HA automation guards:

| Sensor | Values | Meaning |
|---|---|---|
| `waterer_state` | `idle`, `priming`, `watering`, `fault`, `unknown` | Single busy/fault state for the whole controller |
| `active_zone` | `none`, `zone_1` ... `zone_5`, `unknown` | Zone currently owned by the FSM |

`active_zone` means the zone currently being primed/watered, or the faulted
zone when a fault happened after a zone was opened. A precheck fault such as low
water reports `waterer_state=fault` and `active_zone=none`.

HA watering automations should check `waterer_state == idle` before calling a
zone switch. Firmware still rejects overlapping zone commands without raising a
fault, so this sensor is a clean automation guard rather than the only safety
interlock.

### Watering Durations

Each zone has a writable duration number:

| Entity | Range | Meaning |
|---|---|---|
| `duration_zone_1` ... `duration_zone_5` | 1-1800 seconds | Duration used by the next `On` command for that zone |

The firmware initializes every zone to `config::pump::DEFAULT_WATERING_DURATION_SEC`
(15 seconds). Zigbee2MQTT writes duration changes to the zone endpoint's Analog
Output `presentValue` attribute. The firmware clamps all values to
`MIN_WATERING_DURATION_SEC` through `MAX_WATERING_DURATION_SEC`, so automations
can safely set the duration before turning on a zone.

In the converter, each HA number is exposed as `duration_zone_N`. Zigbee2MQTT
passes the normalized key `duration` plus endpoint context into `convertSet`;
the converter writes `genAnalogOutput.presentValue` on endpoint 10-14 and
returns the base `duration` state so Z2M publishes the suffixed property.

Use the HA `Clear fault` button after a notification/acknowledgement automation
has captured the fault. Internally, the button sends an `On` command to endpoint
43. The firmware clears the latched fault and immediately resets endpoint 43
back to `Off`, making the button momentary and repeatable.

Example HA automation action:

```yaml
action: button.press
target:
  entity_id: button.0x1051dbfffe0d375c_clear_fault
```

### Firmware Logs

The serial log is intended to make field debugging possible without packet
sniffing. Current notable messages include:

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
Stopping pump
Waiting 100 ms before closing solenoid
Closing all solenoids
Waiting 2000 ms before disabling Renogy load
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

Water-level logging occurs on the same cadence as the Zigbee water-level report,
currently `config::renogy::POLL_INTERVAL_MS` (30 seconds).

---

## Status LED

The onboard ESP32-C6 WS2812 on GPIO8 mirrors the firmware state:

| State | LED behavior |
|---|---|
| Booting / joining | Slow amber pulse |
| Joined / idle | Brief green heartbeat every 5 seconds |
| Watering | Solid cyan |
| Fault | Rapid red blink |

---

## Safety Invariants

Hard constraints enforced in firmware regardless of HA state:

- **Never dead-head the pump** — a solenoid must be open before the pump starts.
  The FSM guarantees this by waiting one `config::solenoid::PULL_IN_MS` interval after `setLoad(true)`, then waiting for `zones_.open(...)` to finish the solenoid pull-in interval before starting the pump.
- **Ordered shutdown** — active watering stops in reverse order: pump off,
  wait `PUMP_STOP_TO_SOLENOID_CLOSE_MS`, close solenoids, wait
  `SOLENOID_CLOSE_TO_LOAD_DISABLE_MS`, then disable the Renogy load output.
- **Hardware cutoff** — pulling `DRV_MASTER_EN_PIN` LOW cuts all three BTS7960
  boards instantly. Normal firmware faults use the ordered shutdown sequence so
  the pump stops before the valve closes and the Renogy load turns off last.
- **Battery gate** — watering is blocked below `MIN_BATTERY_SOC_PCT` (15%) to
  prevent deep discharge of the LiFePO4 battery.
- **Water level gate** — watering is blocked at or below `MIN_WATER_LEVEL_PCT` (5%).

---

## Troubleshooting

- If Zigbee2MQTT hangs during startup after changing the endpoint shape, back up
  the Z2M data directory and remove only the stale entry for this device from
  Zigbee2MQTT state/database files before restarting. Stale endpoint/reporting
  metadata can keep Z2M trying to access endpoints that no longer exist.
- For firmware-side stale Zigbee state, set
  `config::zigbee::ERASE_NVRAM_ON_BOOT = true`, flash once, let the device boot,
  then set it back to `false` and flash again.
