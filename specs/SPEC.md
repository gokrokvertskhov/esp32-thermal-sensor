# Spec: ESP32-S3 Thermal Camera Server (`thermal_server`)

- **Status:** Approved (Phase 1 Frozen)
- **Methodology:** Spec-Driven Development (`spec-driven-development`)
- **Target Hardware:** Waveshare ESP32-S3-POE-ETH-8DI-8DO + Waveshare MLX90640 Thermal Camera

---

## Assumptions

The following assumptions are made based on project hardware and clarified requirements:
1. **I2C Bus Assignment:** The MLX90640 (`0x33`) and onboard PCF85063ATL RTC (`0x51`) share the primary I2C bus on **GPIO 42 (`SDA`)** and **GPIO 41 (`SCL`)** using ESP-IDF's new `driver/i2c_master.h` API (400 kHz for RTC, 400 kHz–800 kHz for MLX90640).
2. **Flash Partition Layout:** A custom `partitions.csv` on the 16 MB Flash will allocate a dedicated `storage` partition (SPIFFS/LittleFS, mounted at `/spiffs`) containing `/spiffs/config.json` and static web UI assets (`/spiffs/index.html`).
3. **Memory Allocation:** Frame buffers, MLX90640 calibration parameters (`paramsMLX90640`), and JSON serialization buffers will utilize the 8 MB Octal PSRAM (or static DMA-safe internal RAM where required by I2C transactions) to avoid FreeRTOS task stack overflows.
4. **Authentication / Security:** The local HTTP/WebSocket server operates on a trusted local Wi-Fi network without user authentication in v1 (open HTTP on port 80), while Wi-Fi STA uses WPA2-PSK (or WPA/WPA2 mixed mode) configured via `/spiffs/config.json`.

---

## 1. Objective

Build an embedded firmware application (`thermal_server`) for the **Waveshare ESP32-S3-POE-ETH-8DI-8DO** board that continuously acquires 32×24 far-infrared temperature frames from a **Waveshare MLX90640** camera over I2C, timestamps each frame using the onboard **PCF85063 RTC** (synchronized via SNTP when online), indicates system state via the onboard **WS2812 RGB LED**, and serves both a live browser-based heatmap viewer and a JSON REST/WebSocket API over **Wi-Fi**.

### Functional Requirements

| ID | Requirement | Description |
| :--- | :--- | :--- |
| **FR-001** | **Configuration Loading** | Mount the flash filesystem partition (`/spiffs`) on boot and parse `/spiffs/config.json` for Wi-Fi (`ssid`, `password`, optional `hostname`), thermal sensor (`refresh_rate_hz`, `emissivity`), and time (`ntp_server`, `timezone`) settings. Fall back to safe defaults for optional fields; halt with LED error state if `ssid` is missing. |
| **FR-002** | **Wi-Fi Station Connectivity** | Connect to the configured Wi-Fi AP using WPA/WPA2-PSK. Automatically reconnect with exponential backoff (1 s to 30 s) if disconnected. |
| **FR-003** | **RTC & NTP Time Sync** | Read initial system time from the onboard **PCF85063ATL RTC** (`0x51` on I2C) on boot. Once Wi-Fi acquires an IP, synchronize system time via SNTP and update the PCF85063 hardware RTC. |
| **FR-004** | **MLX90640 Thermal Acquisition** | Initialize the **MLX90640** (`0x33` on I2C GPIO 42/41), extract and parse EEPROM calibration parameters, configure the refresh rate (default **4 Hz**, configurable 1–8 Hz), and support both **continuous background acquisition** and **on-demand single-frame capture** (acquiring both subpage 0 and subpage 1 to compute all 768 compensated pixel temperatures in °C plus `min_c`, `max_c`, `avg_c`, `ambient_c`). |
| **FR-005** | **HTTP & WebSocket Server** | Host an `esp_http_server` on port 80 providing:<br>• `GET /` — Live HTML5 Canvas heatmap web interface with start/stop continuous controls, single-shot capture button, color-scale selector, min/max/cursor temperature readout, and FPS counter.<br>• `GET /api/v1/status` — Device health, uptime, Wi-Fi RSSI, IP, RTC timestamp, sensor config, and `acquisition_running` state.<br>• `GET /api/v1/frame` — Latest cached 768-pixel temperature array and statistics as JSON.<br>• `WS /api/v1/stream` — Real-time WebSocket stream pushing thermal frames while continuous acquisition is active. |
| **FR-006** | **RGB LED Status Indication** | Drive the onboard WS2812 RGB LED (**GPIO 38**) to reflect system state:<br>• **Blue (pulsing):** Booting / connecting to Wi-Fi<br>• **Green (solid):** Wi-Fi connected & continuous acquisition active<br>• **Cyan (solid):** Wi-Fi connected & sensor idle (continuous acquisition stopped)<br>• **Yellow (blinking):** Wi-Fi disconnected (retrying)<br>• **Red (blinking):** Critical fault (config missing or I2C sensor communication failure) |
| **FR-007** | **Continuous Acquisition Control API** | Provide HTTP API endpoints to start and stop continuous thermal image acquisition at runtime:<br>• `POST /api/v1/acquisition/start` — Starts continuous background polling of the MLX90640 and WebSocket broadcasting (idempotent if already running; optionally accepts `{"refresh_rate_hz": <1..8>}`).<br>• `POST /api/v1/acquisition/stop` — Stops continuous background polling and places the acquisition task in an idle wait state (idempotent if already stopped). |
| **FR-008** | **On-Demand Single Frame Capture API** | Provide an HTTP API endpoint (`POST /api/v1/frame/capture` and `GET /api/v1/frame/capture`) to trigger an immediate, fresh thermal frame capture from the MLX90640 when continuous acquisition is **not** running:<br>• When continuous acquisition is **stopped**: wakes the sensor acquisition pipeline to read both MLX90640 subpages (0 and 1), computes the 768-pixel temperature matrix, updates the cached frame, and returns `200 OK` with the new frame JSON.<br>• When continuous acquisition is **running**: returns `409 Conflict` indicating continuous acquisition is already active (clients should use `GET /api/v1/frame` or first stop continuous mode). |
| **FR-009** | **USB Serial/JTAG Debug Console & CLI** | Enable the ESP32-S3 built-in **USB Serial/JTAG** interface (`/dev/ttyACM0`, `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`) for both real-time structured debug logging (`ESP_LOG*`) and an interactive `esp_console` diagnostic shell supporting commands: `status` (print IP, Wi-Fi, RTC, and sensor state), `i2c_scan` (probe I2C bus addresses `0x20`, `0x33`, `0x51`), `acq start|stop` (control continuous acquisition), `capture` (trigger single-shot frame and print summary stats), `heap` (memory/PSRAM stats), and `log_level <tag> <level>`. |
| **FR-010** | **Automated Integration Test Suite** | Provide a host-side Python `pytest` integration test suite (`tests/integration/`) that reads a configuration file (`tests/integration/test_config.json`) containing the target `device_ip`, `http_port`, and USB `serial_port` (`/dev/ttyACM0`). The suite validates end-to-end functionality across both Wi-Fi (HTTP REST + WebSocket) and the USB Serial debug console. |

---

## 2. Tech Stack

- **Framework:** Espressif ESP-IDF `v6.1`
- **Language:** C (C17 standard, `-std=gnu17`)
- **Target SoC:** `esp32s3` (ESP32-S3-WROOM-1U-N16R8: Dual-core LX7 @ 240 MHz, 16 MB Flash, 8 MB Octal PSRAM)
- **Core ESP-IDF Components:**
  - `esp_driver_i2c` (`driver/i2c_master.h` — new I2C master driver)
  - `esp_wifi`, `esp_netif`, `esp_event`, `nvs_flash` (Wi-Fi STA & network stack)
  - `esp_http_server` (HTTP + WebSocket support enabled via `CONFIG_ESP_HTTPSERVER_WS_SUPPORT=y`)
  - `console` (`esp_console` over USB Serial/JTAG `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` for interactive serial debugging)
  - `spiffs` or `littlefs` (Flash filesystem for `/spiffs/config.json` and web assets)
  - `cjson` (JSON config parsing and API serialization)
  - `espressif/led_strip` (RMT-backed WS2812 RGB LED driver on GPIO 38)
  - `lwip` (`esp_netif_sntp` for NTP time synchronization)
- **Thermal Sensor Driver:** Melexis `mlx90640-library` (ported to ESP-IDF `i2c_master_transmit` / `i2c_master_transmit_receive`)
- **Host Integration Test Stack:** Python 3, `pytest`, `requests`, `websocket-client`, `pyserial`

---

## 3. Commands

```bash
# Configure project for esp32s3 (using default preset)
idf.py --preset default reconfigure

# Build firmware and storage partition image
idf.py -B build/default build

# Run target/host Unity unit tests
idf.py -B build/test -T all build

# Flash firmware + partition table + filesystem image via USB JTAG/Serial
idf.py -B build/default -p /dev/ttyACM0 flash

# Open interactive USB Serial/JTAG debug console & log monitor
idf.py -B build/default -p /dev/ttyACM0 monitor

# Run automated integration test suite (reads tests/integration/test_config.json)
pytest tests/integration/ -v --tb=short

# Check static analysis / clang-tidy via compile_commands.json
clang-tidy -p build/default main/*.c
```

---

## 4. Project Structure

```text
thermal_server/
├── CMakeLists.txt              # Top-level ESP-IDF project definition
├── CMakePresets.json           # CMake presets (default, production)
├── partitions.csv              # Custom 16MB partition table (nvs, phy_init, factory, storage)
├── sdkconfig.defaults          # Hardware-specific defaults (16MB Flash, 8MB Octal PSRAM, USB Serial/JTAG, WS)
├── spiffs_data/                # Files packaged into the /spiffs flash partition
│   ├── config.example.json     # Template configuration file
│   ├── config.json             # Active Wi-Fi & sensor configuration (gitignored for secrets)
│   └── index.html              # Embedded single-page thermal heatmap viewer
├── specs/                      # SDD specifications, plans, and task lists
│   └── SPEC.md                 # Master project specification (this document)
├── components/
│   ├── app_config/             # SPIFFS mounting and config.json parser
│   ├── debug_console/          # USB Serial/JTAG esp_console REPL & diagnostic CLI commands
│   ├── mlx90640/               # Melexis MLX90640 I2C driver & temperature calculation
│   ├── pcf85063/               # Onboard PCF85063ATL RTC I2C driver & SNTP sync
│   ├── status_led/             # WS2812 RGB LED state machine (GPIO 38)
│   ├── wifi_manager/           # Wi-Fi STA connection & auto-reconnect manager
│   └── web_server/             # HTTP REST + WebSocket thermal streaming server
├── main/
│   ├── CMakeLists.txt          # Main component registration
│   └── main.c                  # System initialization and FreeRTOS task orchestration
├── test/                       # Embedded Unity-based unit tests (config parser, thermal math)
└── tests/
    └── integration/            # Host-side Python pytest integration test suite
        ├── requirements.txt    # pytest, requests, websocket-client, pyserial
        ├── test_config.json    # Target device IP, HTTP port, and USB serial port config
        ├── conftest.py         # Fixtures for config loading, HTTP client, and serial debug interface
        ├── test_serial_cli.py  # Validates USB Serial/JTAG CLI commands (status, i2c_scan, heap)
        └── test_thermal_api.py # Validates /status, /acquisition/start|stop, /frame/capture, /frame, WS /stream
```

---

## 5. Configuration & API Contracts

### 5.1 Configuration File (`/spiffs/config.json`)
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

### 5.2 HTTP & WebSocket API Endpoints

| Method & Path | Description | Behavior & Status Codes |
| :--- | :--- | :--- |
| `GET /` | Embedded Web UI | Serves `/spiffs/index.html` with live heatmap canvas, Start/Stop continuous buttons, and Single-Shot Capture button. |
| `GET /api/v1/status` | System & Sensor Status | `200 OK` — Returns uptime, Wi-Fi RSSI/IP, RTC time, `acquisition_running` boolean, `refresh_rate_hz`, and `last_frame_seq`. |
| `POST /api/v1/acquisition/start` | Start Continuous Mode | `200 OK` — Starts continuous thermal acquisition (optional JSON body `{"refresh_rate_hz": 4}`). Returns updated acquisition state. |
| `POST /api/v1/acquisition/stop` | Stop Continuous Mode | `200 OK` — Stops continuous thermal acquisition. Returns updated acquisition state. |
| `GET /api/v1/frame` | Get Latest Cached Frame | `200 OK` — Returns the most recently acquired frame from memory (`404 Not Found` if no frame has been captured yet since boot). |
| `POST /api/v1/frame/capture`<br>`GET /api/v1/frame/capture` | Capture New Frame On-Demand | • If continuous mode is **stopped**: `200 OK` — Triggers an immediate hardware capture of subpages 0 & 1 on the MLX90640, updates the cached frame, and returns the fresh frame JSON.<br>• If continuous mode is **running**: `409 Conflict` — Returns `{"error": "continuous_acquisition_active", "message": "Stop continuous acquisition before requesting a single-shot capture, or use GET /api/v1/frame"}`. |
| `WS /api/v1/stream` | Live Frame Stream | Pushes JSON frame payloads to connected WebSocket clients whenever a new frame is acquired (either continuously or via on-demand capture). |

### 5.3 `POST /api/v1/acquisition/start` & `/stop` Response Payload
```json
{
  "acquisition_running": true,
  "refresh_rate_hz": 4,
  "emissivity": 0.95
}
```

### 5.4 `GET /api/v1/frame` & `POST /api/v1/frame/capture` Frame Payload
```json
{
  "seq": 1042,
  "mode": "continuous",
  "timestamp_ms": 1794686400125,
  "iso_time": "2026-11-14T20:00:00Z",
  "width": 32,
  "height": 24,
  "ambient_c": 24.3,
  "min_c": 21.8,
  "max_c": 36.4,
  "avg_c": 25.1,
  "pixels": [22.1, 22.3, "... (768 floats rounded to 2 decimal places)"]
}
```
*(Note: `"mode"` is `"continuous"` when captured by the continuous loop, or `"single"` when captured via `/api/v1/frame/capture`.)*

### 5.5 Integration Test Configuration (`tests/integration/test_config.json`)
```json
{
  "device_ip": "192.168.1.100",
  "http_port": 80,
  "http_timeout_sec": 5.0,
  "serial": {
    "enabled": true,
    "port": "/dev/ttyACM0",
    "baudrate": 115200,
    "timeout_sec": 3.0
  },
  "expectations": {
    "width": 32,
    "height": 24,
    "min_valid_temp_c": -40.0,
    "max_valid_temp_c": 300.0
  }
}
```
*(Tip: If `"device_ip"` is set to `"auto"` and `"serial.enabled"` is `true`, the test fixture queries the `status` command over `/dev/ttyACM0` to discover the device's live Wi-Fi IP automatically.)*

### 5.6 USB Serial/JTAG Debug CLI Commands (`/dev/ttyACM0`)

| CLI Command | Description | Example Output |
| :--- | :--- | :--- |
| `status` | Prints JSON/key-value summary of Wi-Fi IP, RSSI, RTC time, and sensor state | `IP=192.168.1.100 RSSI=-52dBm ACQ=running FPS=4 SEQ=128` |
| `i2c_scan` | Probes I2C0 (`GPIO 42` SDA / `GPIO 41` SCL) for active addresses | `Found I2C devices: 0x20 (TCA9554), 0x33 (MLX90640), 0x51 (PCF85063)` |
| `acq start [hz]` | Starts continuous acquisition (optional refresh rate 1..8 Hz) | `Continuous acquisition started (4 Hz)` |
| `acq stop` | Stops continuous acquisition | `Continuous acquisition stopped` |
| `capture` | Performs single-shot capture (when continuous is stopped) and prints min/max/avg | `Captured seq=129 min=22.1C max=34.8C avg=24.6C ambient=23.9C` |
| `heap` | Prints internal SRAM and Octal PSRAM free/minimum-free heap | `DRAM free=245120B PSRAM free=8124000B` |
| `log_level <tag> <lvl>` | Dynamically adjusts runtime `esp_log_level_set()` over serial | `Set log level for mlx90640 to DEBUG` |

---

## 6. Code Style

- **Naming Conventions:**
  - Modules/Functions: `snake_case` prefixed with module name (e.g., `mlx90640_get_frame()`, `wifi_mgr_connect()`).
  - Types/Structs: `snake_case_t` (e.g., `thermal_frame_t`, `app_config_t`).
  - Constants/Macros: `UPPER_SNAKE_CASE` (e.g., `I2C_MASTER_SCL_IO`, `MLX90640_I2C_ADDR`).
- **Error Handling:** All hardware and initialization functions return `esp_err_t` and use `ESP_RETURN_ON_ERROR` / `ESP_LOGE(TAG, ...)` from `esp_check.h`.
- **Example Snippet:**

```c
#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "i2c_bus";

#define BOARD_I2C_SCL_GPIO  (GPIO_NUM_41)
#define BOARD_I2C_SDA_GPIO  (GPIO_NUM_42)

esp_err_t board_i2c_master_init(i2c_master_bus_handle_t *out_bus_handle)
{
    ESP_RETURN_ON_FALSE(out_bus_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "bus handle is NULL");

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, out_bus_handle), TAG, "Failed to init I2C master bus");
    ESP_LOGI(TAG, "Initialized I2C0 on SDA=%d, SCL=%d", BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);
    return ESP_OK;
}
```

---

## 7. Testing Strategy

- **Unit Tests (Unity Framework):**
  - Located in component `test/` directories.
  - Validate `app_config` JSON parsing (valid config, missing SSID, malformed JSON, boundary values for `refresh_rate_hz` and `emissivity`).
  - Validate `mlx90640` temperature calculation math against a known reference EEPROM + raw subpage fixture (`min_c`, `max_c`, `avg_c`, NaN/outlier check).
  - Validate JSON frame serializer formatting and buffer bounds.
- **Automated Host-Side Integration Test Suite (`pytest` in `tests/integration/`):**
  - Reads target connection parameters (`device_ip`, `http_port`, and USB `serial.port` `/dev/ttyACM0`) from `tests/integration/test_config.json`.
  - **Serial CLI Tests (`test_serial_cli.py`):** Connects via `pyserial` to `/dev/ttyACM0`, verifies `i2c_scan` detects `0x20`, `0x33`, and `0x51`, checks `status` and `heap` outputs, and monitors for runtime errors or Guru Meditation faults.
  - **REST & WebSocket API Tests (`test_thermal_api.py`):**
    1. Verifies `GET /api/v1/status` returns valid device health and schema.
    2. Calls `POST /api/v1/acquisition/stop` and verifies `acquisition_running == false`.
    3. Calls `POST /api/v1/frame/capture` while stopped, verifying a `200 OK` fresh 768-pixel frame (`mode == "single"`, `seq` increments, all 768 pixels within valid physical range `-40.0` to `300.0` °C, no `NaN`).
    4. Calls `POST /api/v1/acquisition/start` and verifies `acquisition_running == true`.
    5. Verifies `POST /api/v1/frame/capture` returns `409 Conflict` while continuous mode is active.
    6. Verifies `GET /api/v1/frame` returns advancing `seq` numbers (`mode == "continuous"`) and WebSocket `/api/v1/stream` pushes frames at the configured refresh rate.

---

## 8. Boundaries

- **Always do:**
  - Use ESP-IDF's modern `driver/i2c_master.h` API (not legacy `driver/i2c.h`).
  - Check all `esp_err_t` return codes and log errors with component tags.
  - Protect shared frame buffers and I2C sensor access between the continuous acquisition task and on-demand single-shot capture handler using FreeRTOS synchronization primitives (task notification / event group + mutex).
  - Keep real Wi-Fi credentials out of git (`spiffs_data/config.json` gitignored; commit `spiffs_data/config.example.json` instead).
- **Ask first:**
  - Adding third-party managed components not listed in the Tech Stack.
  - Modifying the partition table sizes (`partitions.csv`) once deployed.
  - Changing the I2C pins (`GPIO 41` / `GPIO 42`) or adding authentication to the HTTP server.
- **Never do:**
  - Hardcode Wi-Fi SSIDs or passwords in `.c` or `.h` source files.
  - Allocate large frame arrays (`float[768]`, `uint16_t[834]`) on small FreeRTOS task stacks.
  - Perform concurrent unsynchronized I2C reads on the MLX90640 from multiple tasks (all sensor I2C reads must be serialized through the acquisition task or sensor mutex).

---

## 9. Success Criteria

- [ ] **SC-001:** Firmware compiles cleanly with zero warnings using `idf.py -B build/default build` on ESP-IDF v6.1 (`esp32s3`).
- [ ] **SC-002:** On boot, the device mounts `/spiffs`, reads `/spiffs/config.json`, and connects to the configured WPA2 Wi-Fi AP within 10 seconds.
- [ ] **SC-003:** I2C bus initializes on `GPIO 42` (SDA) and `GPIO 41` (SCL), reads time from PCF85063 (`0x51`), and syncs NTP time back to the RTC when Wi-Fi connects.
- [ ] **SC-004:** MLX90640 (`0x33`) captures 32×24 thermal frames at the configured refresh rate (4–8 Hz) in continuous mode with valid calibrated temperatures (no `NaN` pixels at room temperature).
- [ ] **SC-005:** `GET http://<device-ip>/api/v1/frame` returns a valid JSON payload with the latest cached 768 pixel temperatures, statistics, and RTC/NTP timestamp in `< 50 ms`.
- [ ] **SC-006:** `POST /api/v1/acquisition/stop` stops continuous acquisition, and `POST /api/v1/acquisition/start` starts/resumes continuous acquisition.
- [ ] **SC-007:** When continuous acquisition is stopped, calling `POST /api/v1/frame/capture` (or `GET /api/v1/frame/capture`) triggers a fresh MLX90640 capture (both subpages 0 and 1) and returns the new 768-pixel JSON frame (`mode: "single"`), while returning `409 Conflict` if called while continuous acquisition is active.
- [ ] **SC-008:** Navigating to `http://<device-ip>/` in a browser renders a live 32×24 thermal heatmap streamed over WebSocket (`/api/v1/stream`) with working Start/Stop Continuous and Single-Shot Capture controls.
- [ ] **SC-009:** Onboard WS2812 RGB LED (`GPIO 38`) accurately reflects boot, continuous streaming (green), sensor idle (cyan), Wi-Fi retry (yellow), and fault (red) states.
- [ ] **SC-010:** USB Serial/JTAG console (`/dev/ttyACM0`) outputs structured `ESP_LOG` messages and provides an interactive `esp_console` CLI supporting `status`, `i2c_scan`, `acq start|stop`, `capture`, `heap`, and `log_level`.
- [ ] **SC-011:** Running `pytest tests/integration/ -v` (configured via `tests/integration/test_config.json`) passes 100% of end-to-end tests across both the Wi-Fi HTTP/WebSocket API and the USB Serial debug console.

---

## 10. Open Questions

None blocking Phase 1. Ready for human review to advance to **Phase 2: Plan** and **Phase 3: Tasks**.
