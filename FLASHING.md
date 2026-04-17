# Flashing Guide

This guide covers flashing the Solar Plant Watering System firmware to an ESP32-C6.

## Quick Start (Recommended)

Download the merged binary from the [latest release](https://github.com/vsi5004/Outdoor-Solar-Plant-Watering-System/releases/latest) and flash to address `0x0`:

```bash
esptool.py --chip esp32c6 --port COMX write_flash 0x0 \
  plant-waterer-vX.X.X-XXXXXXX-merged.bin
```

Replace `COMX` with your serial port (e.g. `COM3` on Windows, `/dev/ttyUSB0` on Linux).

## Prerequisites

### Hardware

- ESP32-C6 development board or custom PCB
- USB cable with data lines
- Connected peripherals per the [hardware spec](Docs/plant-waterer-firmware-spec.md):
  BTS7960 motor driver, 5× solenoid valves, YF-S201 flow meter, float sensor,
  Renogy charge controller (Modbus RTU)

### Installing esptool

```bash
pip install esptool
```

Or use the standalone executable from [GitHub releases](https://github.com/espressif/esptool/releases).

## Method 1: Merged Binary (Easiest)

A single file that writes the bootloader, partition table, and application in one pass.

### Step 1: Download

From the [releases page](https://github.com/vsi5004/Outdoor-Solar-Plant-Watering-System/releases), download:

- `plant-waterer-vX.X.X-XXXXXXX-merged.bin`

### Step 2: Flash

```bash
esptool.py --chip esp32c6 --port COMX write_flash 0x0 \
  plant-waterer-vX.X.X-XXXXXXX-merged.bin
```

Optional flags:

- `--baud 921600` — faster upload
- `--before default_reset --after hard_reset` — auto-reset around flash

### Step 3: Verify

The device resets automatically after flashing. Open a serial monitor at 115200 baud
to confirm boot messages appear.

## Method 2: Individual Binaries (Advanced)

Use this when updating only the application partition (preserves NVS, Zigbee storage,
and other data partitions).

### Step 1: Download

From the releases page, download all files:

- `bootloader-XXXXXXX.bin`
- `partition-table-XXXXXXX.bin`
- `plant-waterer-vX.X.X-XXXXXXX.bin`

### Step 2: Flash all partitions (fresh install)

```bash
esptool.py --chip esp32c6 --port COMX write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 2MB \
  0x0    bootloader-XXXXXXX.bin \
  0x8000 partition-table-XXXXXXX.bin \
  0x10000 plant-waterer-vX.X.X-XXXXXXX.bin
```

### Step 3: Application-only update (preserves NVS and Zigbee pairing)

```bash
esptool.py --chip esp32c6 --port COMX write_flash 0x10000 \
  plant-waterer-vX.X.X-XXXXXXX.bin
```

> **Note:** The application-only update preserves your NVS keys (per-zone water
> usage totals) and the Zigbee network credentials stored in `zb_storage`.
> You will not need to re-pair with Home Assistant.

## Method 3: ESP Flash Download Tool (Windows GUI)

1. Download the **ESP Flash Download Tool** from
   [Espressif's site](https://www.espressif.com/en/support/download/other-tools)
2. Launch the tool, select **ChipType: ESP32-C6**, **WorkMode: Develop**
3. Add files with their addresses:

   | File | Address |
   |------|---------|
   | `plant-waterer-vX.X.X-XXXXXXX-merged.bin` | `0x0` |

   **Or** individual binaries:

   | File | Address |
   |------|---------|
   | `bootloader-XXXXXXX.bin` | `0x0` |
   | `partition-table-XXXXXXX.bin` | `0x8000` |
   | `plant-waterer-vX.X.X-XXXXXXX.bin` | `0x10000` |

4. Set **SPI SPEED: 80 MHz**, **SPI MODE: DIO**, **FLASH SIZE: 2 MB**
5. Select your COM port and click **START**

## Partition Layout Reference

| Name | Type | Offset | Size | Notes |
|------|------|--------|------|-------|
| nvs | data/nvs | 0x9000 | 24 KB | Water usage totals, runtime config |
| phy_init | data/phy | 0xF000 | 4 KB | RF calibration data |
| factory | app/factory | 0x10000 | 960 KB | Application firmware |
| zb_storage | data/fat | 0x100000 | 16 KB | Zigbee network credentials |
| zb_fct | data/fat | 0x104000 | 1 KB | Zigbee factory config |

## First-Time Zigbee Setup

After flashing, the device needs to join your Zigbee network:

1. Open Home Assistant → **Settings → Devices & Services → Zigbee Home Automation**
2. Click **Add device** to open the join window (60 seconds)
3. Power on or reset the ESP32-C6 — it will scan for and join the open network
4. The device should appear as **plant-waterer** in the ZHA device list
5. Assign it to a room and configure zone automations as needed

If the device previously joined a network (e.g. after an application-only update),
no re-pairing is required.

## Resetting Zigbee Pairing

To clear stored Zigbee credentials and force re-pairing, erase only the `zb_storage`
and `zb_fct` partitions:

```bash
esptool.py --chip esp32c6 --port COMX erase_region 0x100000 0x4000
esptool.py --chip esp32c6 --port COMX erase_region 0x104000 0x400
```

Then reset the device. It will announce itself as a new node.

## Verifying Checksums

Each release includes `SHA256SUMS.txt`. Verify your downloads before flashing:

```bash
sha256sum --check SHA256SUMS.txt
```

## Troubleshooting

### Device not detected

**Windows:** install [CP210x USB to UART drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
or check Device Manager for unrecognised USB Serial devices.

**Linux/macOS:** ensure your user is in the `dialout` group:

```bash
sudo usermod -a -G dialout $USER
```

Log out and back in, then retry.

### "Failed to connect"

1. Hold the **BOOT** button while plugging in USB, then release after connection
2. Try a different USB cable (many cables are charge-only)
3. Reduce baud rate: `--baud 115200`

### Device resets in a boot loop after flashing

The bootloader or partition table may be corrupt. Re-flash all three components
using Method 2 above. If the problem persists, erase the entire flash first:

```bash
esptool.py --chip esp32c6 --port COMX erase_flash
```

Then flash the merged binary.

### Zigbee device does not appear in Home Assistant

- Confirm ZHA join window is open when the device boots
- Check the serial monitor for `ZB: joined network` log messages
- If the device previously paired to a different coordinator, erase Zigbee
  storage as described in [Resetting Zigbee Pairing](#resetting-zigbee-pairing)

## Building From Source

```bash
git clone https://github.com/vsi5004/Outdoor-Solar-Plant-Watering-System.git
cd Outdoor-Solar-Plant-Watering-System

# Requires ESP-IDF v5.4.1 (idf.py must be on PATH)
idf.py set-target esp32c6
idf.py build
idf.py -p COMX flash monitor
```

See [README.md](README.md) for full build prerequisites.
