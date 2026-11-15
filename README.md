# Thermal Server (`thermal_server`)

Firmware for the **Waveshare ESP32-S3-POE-ETH-8DI-8DO** controller interfacing with a **Waveshare MLX9064X (MLX90640)** 32×24 far-infrared thermal camera over **I2C**, providing live Wi-Fi connectivity, an embedded HTML5 heatmap viewer, a REST & WebSocket streaming API, an interactive USB Serial/JTAG debug console, and an automated Python `pytest` integration test suite.

Built using **Spec-Driven Development (SDD)** with full project specifications and task tracking stored in the [specs/](specs/) folder:
- [specs/SPEC.md](specs/SPEC.md) — Master Technical Specification (FR-001 through FR-010, SC-001 through SC-011)
- [specs/PLAN.md](specs/PLAN.md) — Architecture and Implementation Plan
- [specs/TASKS.md](specs/TASKS.md) — Phased Task Breakdown and Verification Matrix

---

## Key Features

1. **MLX90640 Thermal Acquisition (`0x33` on I2C0):**
   - 32×24 FIR array (768 pixels).
   - EEPROM calibration extraction and temperature compensation calculation.
   - Dual-mode acquisition: **Continuous streaming** (1–8 Hz) and **On-Demand Single-Shot Capture** (reading both subpages 0 & 1 sequentially for complete frame refresh).
2. **Wi-Fi Remote Connectivity & Flash Configuration:**
   - Configuration file stored in the Flash SPIFFS partition at `/spiffs/config.json`.
   - WPA/WPA2-PSK Station mode with automatic exponential backoff reconnection (1 s to 30 s).
3. **Onboard Timekeeping & Status Indication:**
   - Onboard **NXP PCF85063ATL RTC** (`0x51` on I2C0) restores system clock on boot and synchronizes with SNTP once online.
   - Onboard **WS2812 RGB LED** (`GPIO 38`) indicates system states: Booting (Blue pulsing), Streaming (Green solid), Sensor Idle (Cyan solid), Wi-Fi Reconnecting (Yellow blinking), and Fault (Red blinking).
4. **Embedded Web Server & Live Heatmap UI:**
   - Single-page HTML5 Canvas thermal viewer (`/spiffs/index.html`) with Ironbow/Rainbow/Grayscale palettes, bilinear smoothing, and cursor hover temperature inspector.
   - REST API and WebSocket stream (`WS /api/v1/stream`) on port 80.
5. **USB Serial/JTAG Diagnostic Console (`/dev/ttyACM0`):**
   - Built-in ESP32-S3 USB Serial/JTAG controller running an interactive `esp_console` shell (`status`, `i2c_scan`, `acq start|stop`, `capture`, `heap`, `log_level`).
6. **Automated Host-Side Integration Test Suite:**
   - `pytest` suite in `tests/integration/` validating serial console CLI commands and full HTTP/WebSocket API workflows.

---

## Hardware Specifications

### 1. Host Board: Waveshare ESP32-S3-POE-ETH-8DI-8DO

| Feature | Specification |
| :--- | :--- |
| **SoC / Module** | ESP32-S3-WROOM-1U-N16R8 |
| **CPU** | Dual-core Xtensa 32-bit LX7 @ up to 240 MHz |
| **Memory** | 512 KB SRAM, **8 MB Octal PSRAM**, **16 MB Quad SPI Flash** |
| **Wireless** | 2.4 GHz Wi-Fi (802.11 b/g/n) & Bluetooth 5 (BLE) |
| **Power Supply** | Screw terminal DC (7–36 V), USB Type-C (5 V), or IEEE 802.3af PoE |
| **Digital Inputs (8DI)** | 8x Optocoupler-isolated inputs (5–36 V) on `GPIO 4`–`GPIO 11` |
| **Digital Outputs (8DO)** | 8x Optocoupler-isolated Darlington outputs driven via I2C TCA9554 (`0x20`) |
| **Ethernet** | W5500 SPI Ethernet controller (`GPIO 12`–`16`, `GPIO 39`) |
| **Console / Debug** | Built-in USB Serial/JTAG (`/dev/ttyACM0`) |

#### Shared I2C0 Bus (`GPIO 42` SDA / `GPIO 41` SCL)
| 7-Bit Address | Device | Bus Speed | Description |
| :--- | :--- | :--- | :--- |
| **`0x20`** | TCA9554PWR | 400 kHz | 8-Channel Digital Output Expander |
| **`0x33`** | **Waveshare MLX90640** | **800 kHz** | **32×24 Far-Infrared Thermal Sensor** |
| **`0x51`** | PCF85063ATL | 400 kHz | Onboard Real-Time Clock (RTC) |

---

### 2. Thermal Camera: Waveshare MLX90640

- **Array Resolution:** 32 × 24 (768 measurement points)
- **Field of View:** 55°×35° (D55) or 110°×75° (D110)
- **Target Temperature:** -40 °C to +300 °C (accuracy ±1 to ±2 °C)
- **Supply Voltage:** 3.3 V / 5 V (onboard level shifter)
- **Wiring to Host Board:**
  - `VCC` -> `3.3V` or `5V`
  - `GND` -> `GND`
  - `SDA` -> `GPIO 42`
  - `SCL` -> `GPIO 41`

---

## Configuration File (`/spiffs/config.json`)

Stored on the 2 MB `storage` SPIFFS partition:

```json
{
  "wifi": {
    "ssid": "YOUR_WIFI_SSID",
    "password": "YOUR_WPA2_PASSWORD",
    "hostname": "thermal-server"
  },
  "sensor": {
    "refresh_rate_hz": 4,
    "emissivity": 0.95,
    "auto_start_continuous": true
  },
  "time": {
    "ntp_server": "pool.ntp.org",
    "timezone": "PST8PDT,M3.2.0,M11.1.0"
  }
}
```

---

## HTTP REST & WebSocket API Reference

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `GET /` | `GET` | Serves embedded HTML5 Canvas thermal viewer (`spiffs_data/index.html`). |
| `GET /api/v1/status` | `GET` | System health, Wi-Fi status, IP, RTC ISO-8601 time, and acquisition state. |
| `POST /api/v1/acquisition/start` | `POST` | Starts continuous acquisition. Optional body: `{"refresh_rate_hz": 4}`. |
| `POST /api/v1/acquisition/stop` | `POST` | Stops continuous acquisition and transitions sensor to idle state. |
| `GET /api/v1/frame` | `GET` | Returns latest cached 768-pixel temperature frame as JSON. |
| `POST /api/v1/frame/capture`<br>`GET /api/v1/frame/capture` | `POST`/`GET` | Captures a fresh single frame when continuous mode is stopped (`200 OK`). Returns `409 Conflict` if continuous acquisition is currently active. |
| `WS /api/v1/stream` | `WS` | WebSocket stream broadcasting JSON frames to connected clients at sensor refresh rate. |

---

## USB Serial/JTAG Debug Console CLI (`/dev/ttyACM0`)

Connect with `idf.py monitor` (or any terminal emulator at 115200 baud):

```text
thermal> help
  help
  i2c_scan
  heap
  log_level  <tag> <none|error|warn|info|debug|verbose>
  status
  acq        <start [hz]|stop>
  capture

thermal> i2c_scan
Scanning shared I2C bus (SDA=GPIO42, SCL=GPIO41)...
  -> Found device at 0x20 (TCA9554 IO Expander)
  -> Found device at 0x33 (MLX90640 Thermal Camera)
  -> Found device at 0x51 (PCF85063 RTC)
Found 3 I2C device(s).

thermal> heap
Heap Summary:
  Internal DRAM: Free=245120 bytes, Min Free=228400 bytes
  Octal PSRAM:   Free=8124000 bytes, Min Free=8120000 bytes
```

---

## Building, Flashing & Testing

### 1. Build Firmware & Storage Partition
```bash
# Activate ESP-IDF v6.1 environment
source ~/.espressif/tools/activate_idf_v6.1.sh

# Build bootloader, partition table, application, and SPIFFS image
idf.py -B build/default build
```

### 2. Flash to Hardware
```bash
idf.py -B build/default -p /dev/ttyACM0 flash monitor
```

### 3. Run Automated Integration Tests
Configure `tests/integration/test_config.json` with target connection details, then run:
```bash
source ~/.espressif/tools/activate_idf_v6.1.sh
pytest tests/integration/ -v
```
