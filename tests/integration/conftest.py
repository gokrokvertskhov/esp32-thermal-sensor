import os
import json
import time
import re
import pytest

CONFIG_PATH = os.path.join(os.path.dirname(__file__), "test_config.json")

def load_test_config():
    if not os.path.exists(CONFIG_PATH):
        pytest.fail(f"Test config file not found: {CONFIG_PATH}")
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        return json.load(f)

@pytest.fixture(scope="session")
def test_config():
    return load_test_config()

@pytest.fixture(scope="session")
def serial_conn(test_config):
    serial_cfg = test_config.get("serial", {})
    if not serial_cfg.get("enabled", False):
        pytest.skip("Serial connection disabled in test_config.json")

    port = serial_cfg.get("port", "/dev/ttyACM0")
    baudrate = serial_cfg.get("baudrate", 115200)
    timeout = serial_cfg.get("timeout_sec", 3.0)

    if not os.path.exists(port):
        pytest.skip(f"Serial port {port} not found on host")

    import serial
    try:
        ser = serial.Serial(port, baudrate=baudrate, timeout=timeout)
        ser.reset_input_buffer()
    except Exception as e:
        pytest.skip(f"Serial port {port} cannot be accessed or device not ready: {e}")

    def send_cmd(cmd: str, wait_sec: float = 0.5):
        try:
            ser.reset_input_buffer()
            ser.write(f"{cmd}\r\n".encode("utf-8"))
            ser.flush()
            time.sleep(wait_sec)
            lines = []
            while ser.in_waiting:
                line = ser.readline().decode("utf-8", errors="replace").strip()
                if line:
                    lines.append(line)
            return lines
        except Exception as e:
            pytest.skip(f"I/O error communicating with serial device: {e}")

    ser.send_cmd = send_cmd
    yield ser
    try:
        ser.close()
    except Exception:
        pass

@pytest.fixture(scope="session")
def device_base_url(test_config, serial_conn):
    target_ip = test_config.get("device_ip", "auto")
    port = test_config.get("http_port", 80)

    if target_ip == "auto":
        # Discover IP using serial CLI 'status' command
        lines = serial_conn.send_cmd("status", wait_sec=1.0)
        discovered_ip = None
        for line in lines:
            m = re.search(r"IP Address\s*:\s*([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)", line)
            if m:
                discovered_ip = m.group(1)
                break
        if not discovered_ip or discovered_ip == "0.0.0.0" or discovered_ip == "N/A":
            pytest.skip("Could not discover valid device Wi-Fi IP address via serial console")
        target_ip = discovered_ip

    return f"http://{target_ip}:{port}"

@pytest.fixture(scope="session")
def ws_stream_url(device_base_url):
    ws_base = device_base_url.replace("http://", "ws://").replace("https://", "wss://")
    return f"{ws_base}/api/v1/stream"
