# Solar Balcony Plant Watering System — Firmware

ESP32-C6 firmware for a solar-powered, 5-zone balcony plant watering system.
Communicates with Home Assistant via Zigbee (End Device) and manages a pump,
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

## Project Structure

```
plant-waterer/
├── main/
│   ├── main.cpp                  # App entry — wires all components (Phase 11)
│   ├── config.hpp                # constexpr pin defs and constants
│   ├── hal/                      # Pure virtual HAL interfaces (no ESP-IDF deps)
│   │   ├── igpio.hpp             # Digital output (master enable pin)
│   │   ├── ipwm.hpp              # PWM channel (LEDC)
│   │   ├── iadc_channel.hpp      # Single ADC channel (oneshot)
│   │   ├── iuart.hpp             # Byte-stream UART (Modbus)
│   │   └── ipulse_counter.hpp    # Hardware pulse counter (PCNT)
│   ├── drivers/                  # Concrete driver implementations
│   │   ├── bts7960_chip.hpp/.cpp     # IS current sense
│   │   ├── solenoid_actuator.hpp/.cpp # Pull-in / hold sequence
│   │   ├── pump_actuator.hpp/.cpp     # Speed control + IS correction
│   │   ├── flow_meter.hpp/.cpp        # Pulse count → mL
│   │   ├── float_sensor.hpp/.cpp      # ADC → water level %
│   │   └── renogy_driver.hpp/.cpp     # Modbus RTU + thread-safe data
│   ├── watering/                 # Zone manager and FSM
│   │   ├── zone_id.hpp
│   │   ├── fault_code.hpp
│   │   ├── zone_status.hpp
│   │   ├── watering_request.hpp
│   │   ├── zone_manager.hpp/.cpp
│   │   └── watering_fsm.hpp/.cpp
│   └── zigbee/
│       ├── zb_device.hpp/.cpp
│       └── zb_handlers.hpp/.cpp
├── test/
│   ├── host/                     # Linux target — runs in Docker / CI
│   │   ├── CMakeLists.txt
│   │   └── main/
│   │       ├── CMakeLists.txt
│   │       ├── test_runner.cpp   # UNITY_BEGIN/END + run_xxx_tests() calls
│   │       ├── test_placeholder.cpp
│   │       ├── test_config.cpp
│   │       ├── test_mocks.cpp
│   │       ├── test_bts7960.cpp
│   │       └── test_flow_meter.cpp
│   ├── target/                   # ESP32-C6 target — HIL only
│   │   ├── CMakeLists.txt
│   │   └── main/
│   │       ├── CMakeLists.txt
│   │       └── test_runner.cpp
│   └── mocks/                    # Header-only test doubles
│       ├── mock_gpio.hpp
│       ├── mock_pwm.hpp
│       ├── mock_adc_channel.hpp
│       ├── mock_uart.hpp
│       └── mock_pulse_counter.hpp
├── scripts/
│   └── run_host_tests.sh         # Docker-based host test runner
├── .github/workflows/ci.yml
├── .vscode/
├── Docs/
├── CMakeLists.txt
└── README.md
```

---

## Endpoint Layout (Zigbee)

| EP | Entity | Cluster | Access |
|---|---|---|---|
| 1-5 | Zone 1-5 | On/Off + Analog Output (duration) | R/W |
| 6 | Pump | On/Off | R |
| 10-14 | Zone 1-5 status | Analog Input | R |
| 15-19 | Zone 1-5 ml dispensed | Analog Input | R |
| 20 | Water level % | Analog Input | R |
| 21 | Flow rate mL/min | Analog Input | R |
| 30 | Battery SOC % | Power Config | R |
| 31 | Battery voltage V | Analog Input | R |
| 32 | PV voltage V | Analog Input | R |
| 33 | PV power W | Analog Input | R |
| 40 | Pump current mA | Analog Input | R |
| 41 | Fault code | Analog Input | R |

---

## Safety Invariants

Hard constraints enforced in firmware regardless of HA state:

- **Never dead-head the pump** — a solenoid must be open before the pump starts.
  The FSM guarantees this: `WS_OPEN_SOLENOID` always precedes `WS_START_PUMP`.
- **Emergency stop** — pulling `DRV_MASTER_EN_PIN` LOW cuts all three BTS7960
  boards instantly. The FSM does this first on any fault transition.
- **Battery gate** — watering is blocked below `MIN_BATTERY_SOC_PCT` (15%) to
  prevent deep discharge of the LiFePO4 battery.
- **Water level gate** — watering is blocked below `MIN_WATER_LEVEL_PCT` (10%).
