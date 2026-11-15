# Implementation Plan: ESP32-S3 Thermal Camera Server (`thermal_server`)

- **Status:** Approved (Phase 2 Frozen)
- **Spec Reference:** [specs/SPEC.md](file:///home/georgy/workspaces/ESP32/ThermalSensor/thermal_server/specs/SPEC.md)
- **Methodology:** Spec-Driven Development (`spec-driven-development` / `planning-and-task-breakdown`)

---

## 1. Overview

Implement an ESP-IDF v6.1 firmware application (`thermal_server`) for the **Waveshare ESP32-S3-POE-ETH-8DI-8DO** board that reads configuration from `/spiffs/config.json`, connects to a WPA2 Wi-Fi AP, communicates over the shared I2C0 bus (`GPIO 42` SDA / `GPIO 41` SCL) with the **Waveshare MLX90640** thermal camera (`0x33`) and **PCF85063ATL** RTC (`0x51`), exposes an interactive **USB Serial/JTAG** debug console (`/dev/ttyACM0`), and serves a live HTML5 heatmap UI and REST/WebSocket API (supporting both continuous streaming start/stop and on-demand single-frame capture) validated by a Python `pytest` integration test suite.

---

## 2. Key Architecture Decisions

1. **Single Shared `i2c_master_bus_handle_t` with Per-Device Speeds:**
   - **Decision:** Initialize `I2C_NUM_0` (`SDA=GPIO 42`, `SCL=GPIO 41`) once in a central board/bus module and pass the `i2c_master_bus_handle_t` to `pcf85063` (`scl_speed_hz = 400000`) and `mlx90640` (`scl_speed_hz = 800000`).
   - **Rationale:** ESP-IDF v6.1's `driver/i2c_master.h` is thread-safe per bus handle and supports per-device clock speeds, avoiding bus re-initialization conflicts between the RTC (`0x51`), I/O expander (`0x20`), and MLX90640 (`0x33`).
2. **Event-Driven Thermal Acquisition Task (Continuous vs. Single-Shot):**
   - **Decision:** Dedicate a single FreeRTOS task (`mlx_acq_task`, pinned to Core 1, stack in PSRAM/DRAM) that owns all MLX90640 I2C reads and floating-point compensation math.
   - **State Machine:**
     - **Continuous Mode (`ACQ_MODE_CONTINUOUS`):** Polls the MLX90640 status register at the configured refresh rate (4–8 Hz), reads Subpage 0 and Subpage 1, updates the shared `thermal_frame_t` double buffer under a mutex, and notifies WebSocket broadcast listeners.
     - **Stopped / Idle Mode (`ACQ_MODE_STOPPED`):** Blocks on a FreeRTOS command queue / event group (`xEventGroupWaitBits`).
     - **On-Demand Single Capture (`CMD_CAPTURE_SINGLE`):** When `POST /api/v1/frame/capture` (or CLI `capture`) is called while stopped, the caller posts a synchronous capture request to `mlx_acq_task` and waits on a completion binary semaphore (timeout 1500 ms). `mlx_acq_task` wakes, reads both subpages 0 and 1 sequentially, computes the 768 temperatures (`mode = "single"`), updates the cached frame, and signals completion. If `ACQ_MODE_CONTINUOUS` is active, the HTTP handler immediately returns `409 Conflict` without touching the I2C bus.
3. **PSRAM Allocation for Frame & JSON Buffers:**
   - **Decision:** Allocate `paramsMLX90640` (~3.5 KB), raw frame working buffers (`uint16_t[834]`), compensated pixel buffers (`float[768]`), and HTTP/WebSocket JSON serialization buffers (`~12 KB`) using `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`.
   - **Rationale:** Preserves internal SRAM for Wi-Fi/lwIP DMA buffers and prevents stack overflows inside `esp_http_server` httpd worker tasks.
4. **Non-Blocking USB Serial/JTAG `esp_console` REPL:**
   - **Decision:** Use `esp_console_new_repl_usb_serial_jtag()` on `/dev/ttyACM0` alongside standard `ESP_LOG` output.
   - **Rationale:** Enables live hardware inspection (`i2c_scan`, `status`, `acq start|stop`, `capture`, `heap`) and automated IP discovery by `pytest` without requiring an external UART-to-USB dongle.

---

## 3. Component Dependency Graph

```text
┌─────────────────────────────────────────────────────────────────┐
│             Board & SDK Foundation (Task Group 1)               │
│  partitions.csv + sdkconfig.defaults + SPIFFS + I2C0 Bus Init   │
└───────┬─────────────────┬──────────────────┬────────────────────┘
        │                 │                  │
        ▼                 ▼                  ▼
┌───────────────┐ ┌───────────────┐ ┌─────────────────────────────┐
│  app_config   │ │  status_led   │ │          pcf85063           │
│ (/spiffs JSON)│ │ (WS2812 GPIO38│ │ (RTC 0x51 + SNTP Time Sync) │
└───────┬───────┘ └───────┬───────┘ └──────────────┬──────────────┘
        │                 │                        │
        ├─────────────────┼────────────────────────┤
        │                 ▼                        ▼
        │         ┌───────────────┐        ┌───────────────┐
        └────────►│ wifi_manager  │        │   mlx90640    │
                  │  (WPA2 STA)   │        │(0x33 Acq Task)│
                  └───────┬───────┘        └───────┬───────┘
                          │                        │
                          └───────────┬────────────┘
                                      ▼
                    ┌───────────────────────────────────┐
                    │   web_server & debug_console      │
                    │ (REST, WS /stream, HTML UI, CLI)  │
                    └─────────────────┬─────────────────┘
                                      ▼
                    ┌───────────────────────────────────┐
                    │  Host Integration Suite (pytest)  │
                    │  (tests/integration/test_*.py)    │
                    └───────────────────────────────────┘
```

---

## 4. Implementation Phases & Verification Checkpoints

We organize implementation into **4 vertical-slice phases** with explicit verification gates between phases:

### Phase 2.1: Platform Foundation, Config & Status Peripherals (`FR-001`, `FR-006`, `FR-009`)
1. **Board SDK Configuration & Partition Table:** Configure `sdkconfig.defaults` (16 MB Flash, 8 MB Octal PSRAM, USB Serial/JTAG console, WebSocket support) and `partitions.csv` (NVS, PHY, factory app, 2 MB `storage` SPIFFS partition).
2. **`app_config` Component + Unit Tests:** Mount `/spiffs`, parse `/spiffs/config.json` with validation and fallback defaults, and provide `spiffs_data/config.example.json`.
3. **`status_led` Component & Shared I2C Bus + `debug_console` Skeleton:** Initialize WS2812 LED on `GPIO 38`, initialize shared I2C0 master bus (`GPIO 42` SDA / `GPIO 41` SCL), and start USB Serial/JTAG REPL with `i2c_scan`, `heap`, and `log_level` commands.

#### Checkpoint 1 (Foundation & Hardware Bring-Up)
- [ ] Firmware builds cleanly (`idf.py -B build/default build`) and generates `storage.bin` from `spiffs_data/`.
- [ ] `app_config` unit tests pass.
- [ ] On hardware, USB Serial/JTAG (`/dev/ttyACM0`) responds to `i2c_scan` and detects `0x20` (TCA9554), `0x33` (MLX90640), and `0x51` (PCF85063), while the RGB LED pulses Blue on boot.

---

### Phase 2.2: Wi-Fi Connectivity & RTC/NTP Time Synchronization (`FR-002`, `FR-003`)
4. **`pcf85063` RTC Component:** Read/write time registers on `0x51`, restore system `gettimeofday()` clock at boot, and expose a callback to write SNTP-synchronized UTC time back to the hardware RTC.
5. **`wifi_manager` Component:** Connect to the WPA/WPA2 AP from `app_config`, implement exponential backoff reconnect (1 s – 30 s), trigger SNTP synchronization on `IP_EVENT_STA_GOT_IP`, update `status_led`, and register `status` / `wifi` CLI commands.

#### Checkpoint 2 (Network & Time Sync)
- [ ] Device connects to Wi-Fi AP using `/spiffs/config.json` credentials within 10 seconds of boot.
- [ ] `status` CLI command over `/dev/ttyACM0` reports assigned IPv4 address, RSSI, and valid synchronized UTC timestamp from the PCF85063 RTC.

---

### Phase 2.3: MLX90640 Thermal Acquisition Engine (`FR-004`, `FR-007`, `FR-008`)
6. **`mlx90640` Driver & Calibration Math + Unit Tests:** Port Melexis I2C register access to ESP-IDF `i2c_master_transmit_receive` (`0x33`), implement EEPROM dump & parameter extraction (`MLX90640_ExtractParameters`), implement 768-pixel temperature compensation (`MLX90640_CalculateTo`), and compute `min_c`, `max_c`, `avg_c`, and `ambient_c`.
7. **Thermal Acquisition Task & Mode Controller (`acq_controller`):** Implement `mlx_acq_task` supporting `start_continuous(hz)`, `stop_continuous()`, `capture_single(out_frame, timeout_ms)`, thread-safe cached frame reader `get_latest_frame(out_frame)`, and Serial CLI commands `acq start|stop` and `capture`.

#### Checkpoint 3 (Thermal Acquisition Engine)
- [ ] Unit tests for MLX90640 calibration/temperature math pass without `NaN` values.
- [ ] Over `/dev/ttyACM0`, running `acq stop` followed by `capture` acquires subpages 0 & 1 and prints valid room-temperature `min`/`max`/`avg`/`ambient` values; running `acq start 4` continuously increments `seq` at 4 Hz.

---

### Phase 2.4: HTTP/WebSocket Server, Web UI & Host Integration Test Suite (`FR-005`, `FR-007`, `FR-008`, `FR-010`)
8. **`web_server` REST & WebSocket Endpoints:** Implement `GET /api/v1/status`, `GET /api/v1/frame`, `POST /api/v1/acquisition/start`, `POST /api/v1/acquisition/stop`, `POST|GET /api/v1/frame/capture` (with `409 Conflict` guard when continuous mode is active), and `WS /api/v1/stream` frame broadcaster.
9. **Interactive Web Heatmap UI (`spiffs_data/index.html`):** Build the embedded HTML5 Canvas 32×24 thermal viewer with color palette selector (Ironbow, Rainbow, Grayscale), hover pixel temperature inspector, min/max/avg readout, Start/Stop Continuous buttons, and Single-Shot Capture button.
10. **Host Integration Test Suite (`tests/integration/`):** Create `test_config.json`, `conftest.py`, `test_serial_cli.py`, and `test_thermal_api.py` to automatically verify all serial CLI commands, REST endpoints, continuous start/stop transitions, single-shot capture, and WebSocket frame streaming.

#### Checkpoint 4 (Full System & Integration Verification)
- [ ] Browser UI at `http://<device-ip>/` displays live thermal stream, pauses when "Stop Continuous" is clicked, and updates on "Capture Single Frame".
- [ ] `pytest tests/integration/ -v` passes 100% of test cases against the connected board.

---

## 5. Parallelization Opportunities

Once **Phase 2.1 (Foundation)** and shared header contracts (`app_config.h`, `mlx90640_acq.h`) are frozen:
- **Parallel Track A (Network & Time):** `pcf85063` + `wifi_manager`
- **Parallel Track B (Sensor Engine):** `mlx90640` low-level driver + calibration math + unit tests
- **Parallel Track C (Frontend & Test Harness):** `spiffs_data/index.html` UI and `tests/integration/` pytest suite (built against the frozen JSON API schemas in [specs/SPEC.md](file:///home/georgy/workspaces/ESP32/ThermalSensor/thermal_server/specs/SPEC.md#L110-L204))

---

## 6. Risks and Mitigations

| Risk | Impact | Mitigation |
| :--- | :--- | :--- |
| **Shared I2C0 Bus Contention** between MLX90640 (high-rate frame reads) and PCF85063 RTC (`0x51`) | Medium | Read PCF85063 only at boot and on SNTP sync completion; use `esp_timer` / system clock (`gettimeofday`) for per-frame timestamps during active streaming. |
| **Incomplete Subpage Data** on Single-Shot Capture (MLX90640 updates half the pixels per subpage) | High | In `capture_single()`, explicitly poll and read **both** Subpage 0 and Subpage 1 sequentially before running `MLX90640_CalculateTo()` so all 768 pixels are fresh. |
| **Large JSON Frame Payloads (~6–8 KB)** causing heap fragmentation or slow HTTP responses | Medium | Pre-allocate a reusable PSRAM serialization buffer (`16 KB`) protected by mutex and format floats with `snprintf(..., "%.2f")` rather than building a 768-node `cJSON` linked list on every frame. |
| **USB Serial/JTAG Port Contention** between `idf.py monitor` and `pytest` | Low | Document in `tests/integration/` that `idf.py monitor` should be closed before running `pytest` with `serial.enabled = true`, or allow `serial.enabled = false` for Wi-Fi-only test runs. |
