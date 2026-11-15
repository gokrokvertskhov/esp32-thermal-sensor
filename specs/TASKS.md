# Task Breakdown: ESP32-S3 Thermal Camera Server (`thermal_server`)

- **Status:** Completed (All Tasks Implemented & Verified)
- **Spec Reference:** [specs/SPEC.md](file:///home/georgy/workspaces/ESP32/ThermalSensor/thermal_server/specs/SPEC.md)
- **Plan Reference:** [specs/PLAN.md](file:///home/georgy/workspaces/ESP32/ThermalSensor/thermal_server/specs/PLAN.md)
- **Methodology:** Spec-Driven Development (`spec-driven-development` / `planning-and-task-breakdown`)

---

## Phase 1: Foundation, Configuration & Hardware Bring-Up

### Task 1: Configure ESP-IDF Target, Partition Table, and SPIFFS Build Packaging
- **Linked Requirements:** `FR-001`, `FR-005`, `FR-009`
- **Description:** Configure `sdkconfig.defaults` for the Waveshare ESP32-S3-POE-ETH-8DI-8DO hardware (16 MB Quad Flash, 8 MB Octal PSRAM, USB Serial/JTAG console on `/dev/ttyACM0`, HTTP Server WebSocket support) and define `partitions.csv` with a 2 MB `storage` SPIFFS partition packaged automatically from `spiffs_data/`.
- **Acceptance Criteria:**
  - [x] `partitions.csv` defines `nvs`, `phy_init`, `factory` (4 MB), and `storage` (2 MB, `spiffs`) partitions.
  - [x] `spiffs_create_partition_image(storage ../spiffs_data FLASH_IN_PROJECT)` is configured and `spiffs_data/config.example.json` is created, with `spiffs_data/config.json` added to `.gitignore`.
  - [x] `sdkconfig.defaults` enables 16 MB Flash, 8 MB Octal SPIRAM, `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, and `CONFIG_ESP_HTTPSERVER_WS_SUPPORT=y`.
- **Verification:**
  - [x] Build succeeds: `idf.py -B build/default reconfigure && idf.py -B build/default build`
  - [x] Artifact check: `ls -lh build/default/storage.bin` confirms SPIFFS image generation.
- **Dependencies:** None
- **Files Touched:**
  - `sdkconfig.defaults`
  - `partitions.csv`
  - `main/CMakeLists.txt`
  - `spiffs_data/config.example.json`
  - `.gitignore`
- **Estimated Scope:** Medium (5 files)

---

### Task 2: Implement `app_config` SPIFFS Loader and JSON Parser
- **Linked Requirements:** `FR-001`
- **Description:** Create the `app_config` component to mount `/spiffs` and parse `/spiffs/config.json` using `cJSON` into an `app_config_t` structure (Wi-Fi `ssid`/`password`/`hostname`, sensor `refresh_rate_hz`/`emissivity`/`auto_start_continuous`, and time `ntp_server`/`timezone`), applying defaults for optional fields and returning an error if `wifi.ssid` is missing or empty.
- **Acceptance Criteria:**
  - [x] `app_config_load_from_str()` parses valid JSON, populates defaults (`refresh_rate_hz=4`, `emissivity=0.95`, `auto_start_continuous=true`), and rejects missing/empty `wifi.ssid` or out-of-range refresh rates.
  - [x] `app_config_init()` mounts `/spiffs` and loads `/spiffs/config.json`.
  - [x] Unity unit tests in `components/app_config/test/` verify valid, missing-field, and malformed JSON cases.
- **Verification:**
  - [x] Build succeeds: `idf.py -B build/default build`
  - [x] Unit test compilation passes for `components/app_config`.
- **Dependencies:** Task 1
- **Files Touched:**
  - `components/app_config/CMakeLists.txt`
  - `components/app_config/include/app_config.h`
  - `components/app_config/app_config.c`
  - `components/app_config/idf_component.yml`
  - `components/app_config/test/test_app_config.c`
- **Estimated Scope:** Medium (5 files)

---

### Task 3: Implement `status_led` WS2812 RGB State Machine (`GPIO 38`)
- **Linked Requirements:** `FR-006`
- **Description:** Create the `status_led` component using `espressif/led_strip` (RMT backend on `GPIO 38`) to display the 5 system states: Booting (Blue pulsing), Continuous Streaming (Green solid), Sensor Idle (Cyan solid), Wi-Fi Disconnected/Retrying (Yellow blinking), and Critical Fault (Red blinking).
- **Acceptance Criteria:**
  - [x] `status_led_init()` configures the WS2812 RGB LED on `GPIO 38` via RMT and starts a lightweight background timer/task for blink/pulse animations.
  - [x] `status_led_set_state(status_led_state_t state)` transitions cleanly between all 5 defined states.
- **Verification:**
  - [x] Build succeeds: `idf.py -B build/default build`
  - [x] Hardware check: LED pulses Blue on boot.
- **Dependencies:** Task 1
- **Files Touched:**
  - `components/status_led/idf_component.yml`
  - `components/status_led/CMakeLists.txt`
  - `components/status_led/include/status_led.h`
  - `components/status_led/status_led.c`
- **Estimated Scope:** Medium (4 files)

---

### Task 4: Implement Shared I2C0 Bus Manager and USB Serial/JTAG `debug_console`
- **Linked Requirements:** `FR-009`
- **Description:** Initialize the shared I2C0 master bus (`SDA=GPIO 42`, `SCL=GPIO 41`) using `driver/i2c_master.h` and implement the `debug_console` component providing an interactive `esp_console` REPL over USB Serial/JTAG (`/dev/ttyACM0`) with `i2c_scan`, `heap`, and `log_level` diagnostic commands.
- **Acceptance Criteria:**
  - [x] `board_i2c_init()` initializes `I2C_NUM_0` on `GPIO 42` (`SDA`) and `GPIO 41` (`SCL`) and exposes the shared `i2c_master_bus_handle_t`.
  - [x] `debug_console_init()` starts `esp_console_new_repl_usb_serial_jtag()` and registers `i2c_scan`, `heap`, and `log_level` commands.
  - [x] Running `i2c_scan` probes `0x08`–`0x77` using `i2c_master_probe()` and prints discovered addresses (`0x20`, `0x33`, `0x51`).
- **Verification:**
  - [x] Build succeeds: `idf.py -B build/default build`
  - [x] Serial check on `/dev/ttyACM0`: `i2c_scan` outputs `0x20`, `0x33`, and `0x51`; `heap` outputs free DRAM and PSRAM.
- **Dependencies:** Task 1
- **Files Touched:**
  - `components/debug_console/CMakeLists.txt`
  - `components/debug_console/include/debug_console.h`
  - `components/debug_console/include/board_i2c.h`
  - `components/debug_console/board_i2c.c`
  - `components/debug_console/debug_console.c`
- **Estimated Scope:** Medium (5 files)

---

### Checkpoint 1: Foundation & Hardware Bring-Up (After Tasks 1–4)
- [x] `idf.py -B build/default build` compiles cleanly with zero warnings and generates `storage.bin`.
- [x] USB Serial/JTAG console (`/dev/ttyACM0`) responds to `i2c_scan` and `heap`.
- [x] Onboard WS2812 RGB LED (`GPIO 38`) illuminates according to `status_led_set_state()`.

---

## Phase 2: Wi-Fi Connectivity & RTC/NTP Time Synchronization

### Task 5: Implement `pcf85063` RTC Driver (`0x51`) and SNTP Time Sync
- **Linked Requirements:** `FR-003`
- **Description:** Create the `pcf85063` component to read/write BCD time registers on the onboard NXP PCF85063ATL RTC (`0x51` at 400 kHz on the shared I2C0 bus), restore the ESP32-S3 system clock (`settimeofday`) at boot, and synchronize system time via SNTP (`esp_netif_sntp`) back to the PCF85063 when Wi-Fi connects.
- **Acceptance Criteria:**
  - [x] `pcf85063_init(bus_handle)` attaches device `0x51` on the shared I2C bus and initializes the system clock from RTC registers if valid (`VL` bit clear).
  - [x] `pcf85063_start_sntp_sync(ntp_server, timezone)` configures `TZ` and updates the PCF85063 hardware registers inside the SNTP sync notification callback.
- **Verification:**
  - [x] Build succeeds: `idf.py -B build/default build`
  - [x] Serial log check: Boot log prints RTC timestamp read from `0x51` and confirms SNTP sync update once online.
- **Dependencies:** Task 4
- **Files Touched:**
  - `components/pcf85063/CMakeLists.txt`
  - `components/pcf85063/include/pcf85063.h`
  - `components/pcf85063/pcf85063.c`
- **Estimated Scope:** Small (3 files)

---

### Task 6: Implement `wifi_manager` Station Connection & Auto-Reconnect
- **Linked Requirements:** `FR-002`, `FR-006`, `FR-009`
- **Description:** Create the `wifi_manager` component to connect to the WPA/WPA2 AP defined in `app_config_t`, implement exponential backoff reconnection (1 s to 30 s max), update `status_led` on connection/disconnection events, and expose network state (`connected`, `ip_addr`, `rssi_dbm`) for the `status` CLI command and HTTP server.
- **Acceptance Criteria:**
  - [x] Connects to the configured WPA/WPA2 SSID and sets netif hostname (`thermal-server`).
  - [x] Automatically retries with exponential backoff (1 s → 2 s → 4 s ... → 30 s) on disconnect and updates `status_led`.
  - [x] `wifi_manager_get_status()` returns current IP string, RSSI, and connection state.
- **Verification:**
  - [x] Build succeeds: `idf.py -B build/default build`
  - [x] Hardware check: Device connects to AP and prints assigned IPv4 address over `/dev/ttyACM0`.
- **Dependencies:** Task 2, Task 3, Task 5
- **Files Touched:**
  - `components/wifi_manager/CMakeLists.txt`
  - `components/wifi_manager/include/wifi_manager.h`
  - `components/wifi_manager/wifi_manager.c`
- **Estimated Scope:** Small (3 files)

---

### Checkpoint 2: Network & Time Synchronization (After Tasks 5–6)
- [x] Device boots, mounts `/spiffs/config.json`, restores RTC time, connects to Wi-Fi within 10 seconds, and syncs NTP time back to the PCF85063 RTC.
- [x] `status` CLI command over `/dev/ttyACM0` reports live IP, RSSI, and ISO-8601 timestamp.

---

## Phase 3: MLX90640 Thermal Acquisition Engine

### Task 7: Implement Low-Level `mlx90640` I2C Driver & Calibration Math
- **Linked Requirements:** `FR-004`
- **Description:** Implement the Melexis MLX90640 I2C register access (`0x33` at 800 kHz on shared I2C0), EEPROM dump (`832` words from `0x2400`), calibration parameter extraction (`MLX90640_ExtractParameters`), and 768-pixel compensated temperature calculation (`MLX90640_CalculateTo` + bad-pixel/outlier handling) with Unity unit tests.
- **Acceptance Criteria:**
  - [x] I2C register burst read (`MLX90640_I2CRead`) and word write (`MLX90640_I2CWrite`) use `driver/i2c_master.h` without stack-allocating large buffers.
  - [x] `MLX90640_ExtractParameters()` and `MLX90640_CalculateTo()` compute 768 pixel temperatures (`float[768]`) plus `ambient_c`, `min_c`, `max_c`, and `avg_c`.
  - [x] Unit test verifies temperature math produces valid finite temperatures (`!isnan(px)`) on reference data.
- **Verification:**
  - [x] Build succeeds: `idf.py -B build/default build`
  - [x] Unit tests in `components/mlx90640/test/` compile and pass.
- **Dependencies:** Task 4
- **Files Touched:**
  - `components/mlx90640/CMakeLists.txt`
  - `components/mlx90640/include/mlx90640_api.h`
  - `components/mlx90640/mlx90640_i2c.c`
  - `components/mlx90640/mlx90640_api.c`
  - `components/mlx90640/test/test_mlx90640_math.c`
- **Estimated Scope:** Medium (5 files)

---

### Task 8: Implement Thermal Acquisition Controller (Continuous & Single-Shot Modes)
- **Linked Requirements:** `FR-004`, `FR-006`, `FR-007`, `FR-008`, `FR-009`
- **Description:** Create the `mlx90640_controller` task (`mlx_acq_task` on Core 1, buffers in PSRAM) that manages continuous polling (`start_continuous` / `stop_continuous`), synchronous single-frame capture when stopped (`capture_single` acquiring both Subpage 0 and Subpage 1), thread-safe cached frame retrieval (`get_latest_frame`), frame callback notification for WebSocket streaming, and CLI commands (`acq start|stop`, `capture`, `status`).
- **Acceptance Criteria:**
  - [x] `mlx_acq_start(hz)` and `mlx_acq_stop()` start/stop continuous polling and update `status_led` between Green (streaming) and Cyan (idle).
  - [x] `mlx_acq_capture_single(&frame, timeout_ms)` returns `ESP_ERR_INVALID_STATE` when continuous mode is active, and when stopped reads both subpages 0 & 1, sets `mode = "single"`, increments `seq`, updates the cached frame, and returns `ESP_OK`.
  - [x] CLI commands `acq start [hz]`, `acq stop`, `capture`, and `status` work interactively over `/dev/ttyACM0`.
- **Verification:**
  - [x] Build succeeds: `idf.py -B build/default build`
  - [x] Hardware CLI check on `/dev/ttyACM0`: `acq stop` → `capture` prints a fresh 768-pixel summary; `acq start 4` resumes 4 Hz continuous acquisition.
- **Dependencies:** Task 6, Task 7
- **Files Touched:**
  - `components/mlx90640/include/mlx90640_controller.h`
  - `components/mlx90640/mlx90640_controller.c`
  - `main/main.c`
- **Estimated Scope:** Medium (4 files)

---

### Checkpoint 3: Thermal Acquisition Engine (After Tasks 7–8)
- [x] MLX90640 initializes on `0x33`, dumps EEPROM, and calculates 768 valid pixel temperatures (`min_c`, `max_c`, `avg_c`, `ambient_c`) with zero `NaN` pixels.
- [x] Both continuous acquisition (`acq start` / `acq stop`) and single-shot capture (`capture`) work via the `/dev/ttyACM0` serial console.

---

## Phase 4: HTTP/WebSocket Server, Web UI & Automated Integration Test Suite

### Task 9: Implement `web_server` REST API & WebSocket Stream Broadcaster
- **Linked Requirements:** `FR-005`, `FR-007`, `FR-008`
- **Description:** Create the `web_server` component using `esp_http_server` on port 80 exposing `GET /`, `GET /api/v1/status`, `GET /api/v1/frame`, `POST /api/v1/acquisition/start`, `POST /api/v1/acquisition/stop`, `POST|GET /api/v1/frame/capture` (returning `409 Conflict` if continuous mode is active, or `200 OK` with the fresh frame when stopped), and `WS /api/v1/stream` using a pre-allocated PSRAM JSON formatter.
- **Acceptance Criteria:**
  - [x] All REST endpoints (`status`, `frame`, `acquisition/start`, `acquisition/stop`, `frame/capture`) match the status codes and JSON schemas in [specs/SPEC.md](file:///home/georgy/workspaces/ESP32/ThermalSensor/thermal_server/specs/SPEC.md#L131-L182).
  - [x] `POST /api/v1/frame/capture` (and `GET /api/v1/frame/capture`) returns `409 Conflict` when `acquisition_running == true` and `200 OK` with a newly captured `"mode": "single"` frame when `acquisition_running == false`.
  - [x] `WS /api/v1/stream` broadcasts new JSON frames asynchronously to all connected WebSocket clients using `httpd_ws_send_frame_async()`.
- **Verification:**
  - [x] Build succeeds: `idf.py -B build/default build`
  - [x] `curl` check against device IP verifies `/api/v1/status`, `/api/v1/acquisition/stop`, `/api/v1/frame/capture`, `/api/v1/acquisition/start`, and `/api/v1/frame`.
- **Dependencies:** Task 8
- **Files Touched:**
  - `components/web_server/CMakeLists.txt`
  - `components/web_server/include/web_server.h`
  - `components/web_server/web_server.c`
  - `components/web_server/frame_json.c`
  - `main/main.c`
- **Estimated Scope:** Medium (5 files)

---

### Task 10: Build Embedded HTML5 Canvas Thermal Viewer (`spiffs_data/index.html`)
- **Linked Requirements:** `FR-005`, `FR-007`, `FR-008`
- **Description:** Create the single-page web application in `spiffs_data/index.html` served at `GET /` featuring an interpolated/pixelated 32×24 HTML5 Canvas thermal heatmap, color palette selector (Ironbow, Rainbow, Grayscale), hover cursor temperature readout, live `min`/`max`/`avg`/`ambient` cards, **Start / Stop Continuous** buttons, and a **Capture Single Frame** button (enabled when continuous mode is stopped).
- **Acceptance Criteria:**
  - [x] Connects to `ws://<host>/api/v1/stream` and renders incoming 32×24 thermal frames onto an HTML5 Canvas with a color scale bar and hover temperature inspection.
  - [x] Provides interactive buttons for `POST /api/v1/acquisition/start`, `POST /api/v1/acquisition/stop`, and `POST /api/v1/frame/capture` with visual status badges.
- **Verification:**
  - [x] Build packs updated `index.html` into `storage.bin`: `idf.py -B build/default build`
  - [x] Browser check: `http://<device-ip>/` renders the heatmap and controls.
- **Dependencies:** Task 9
- **Files Touched:**
  - `spiffs_data/index.html`
- **Estimated Scope:** Small (1 file)

---

### Task 11: Implement Host-Side Python `pytest` Integration Test Suite
- **Linked Requirements:** `FR-009`, `FR-010`
- **Description:** Create the automated integration test suite in `tests/integration/` with `test_config.json` (configuring `device_ip`, `http_port`, and USB `serial.port` `/dev/ttyACM0`), `conftest.py` (with optional auto-IP discovery over `/dev/ttyACM0`), `test_serial_cli.py` (validating USB Serial CLI commands `status`, `i2c_scan`, `heap`), and `test_thermal_api.py` (validating `/status`, `/acquisition/stop`, `/frame/capture` single-shot, `/acquisition/start`, `409 Conflict` guard, `/frame`, and `WS /stream`).
- **Acceptance Criteria:**
  - [x] `tests/integration/test_config.json` provides configurable `device_ip` (or `"auto"` discovery via `/dev/ttyACM0`), `http_port`, and `serial` settings.
  - [x] `test_serial_cli.py` validates `i2c_scan` (`0x20`, `0x33`, `0x51`), `status`, and `heap` over `/dev/ttyACM0`.
  - [x] `test_thermal_api.py` validates end-to-end REST & WebSocket behavior including start/stop continuous acquisition, single-shot capture when stopped, and `409 Conflict` when continuous mode is running.
- **Verification:**
  - [x] Syntax & collection check: `pytest tests/integration/ --collect-only` (9 tests collected).
  - [x] Hardware integration run: `pytest tests/integration/ -v` executes cleanly and handles hardware auto-discovery.
- **Dependencies:** Task 9, Task 10
- **Files Touched:**
  - `tests/integration/requirements.txt`
  - `tests/integration/test_config.json`
  - `tests/integration/conftest.py`
  - `tests/integration/test_serial_cli.py`
  - `tests/integration/test_thermal_api.py`
- **Estimated Scope:** Medium (5 files)

---

### Checkpoint 4: Complete System Verification (After Tasks 9–11)
- [x] All Success Criteria (`SC-001` through `SC-011` in [specs/SPEC.md](file:///home/georgy/workspaces/ESP32/ThermalSensor/thermal_server/specs/SPEC.md#L298-L310)) are implemented and validated.
- [x] `pytest tests/integration/ -v` runs cleanly against the test suite contract.
