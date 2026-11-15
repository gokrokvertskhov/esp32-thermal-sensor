import json
import time
import requests
import websocket

def test_api_status(device_base_url, test_config):
    url = f"{device_base_url}/api/v1/status"
    resp = requests.get(url, timeout=test_config["http_timeout_sec"])
    assert resp.status_code == 200

    data = resp.json()
    assert "wifi_connected" in data
    assert "acquisition_running" in data
    assert "refresh_rate_hz" in data
    assert "emissivity" in data
    assert "iso_time" in data
    assert data["wifi_connected"] is True
    exp = test_config["expectations"]
    if "sensor_model" in exp:
        assert data.get("sensor_model") == exp["sensor_model"]
    assert data.get("width") == exp["width"]
    assert data.get("height") == exp["height"]

def test_continuous_acq_control(device_base_url, test_config):
    timeout = test_config["http_timeout_sec"]

    # 1. Stop continuous acquisition
    stop_resp = requests.post(f"{device_base_url}/api/v1/acquisition/stop", timeout=timeout)
    assert stop_resp.status_code == 200
    assert stop_resp.json()["acquisition_running"] is False

    # Verify status reflects stopped
    status_resp = requests.get(f"{device_base_url}/api/v1/status", timeout=timeout)
    assert status_resp.json()["acquisition_running"] is False

    # 2. Restart continuous acquisition at 4 Hz
    start_resp = requests.post(
        f"{device_base_url}/api/v1/acquisition/start",
        json={"refresh_rate_hz": 4},
        timeout=timeout
    )
    assert start_resp.status_code == 200
    assert start_resp.json()["acquisition_running"] is True
    assert start_resp.json()["refresh_rate_hz"] == 4

    # Verify status reflects running
    status_resp = requests.get(f"{device_base_url}/api/v1/status", timeout=timeout)
    assert status_resp.json()["acquisition_running"] is True

def test_single_shot_capture_and_conflict_guard(device_base_url, test_config):
    timeout = test_config["http_timeout_sec"]
    exp = test_config["expectations"]

    # 1. While continuous acquisition is running, single-shot capture must return 409 Conflict
    conflict_resp = requests.post(f"{device_base_url}/api/v1/frame/capture", timeout=timeout)
    assert conflict_resp.status_code == 409
    assert conflict_resp.json()["error"] == "continuous_acquisition_active"

    # 2. Stop continuous mode
    requests.post(f"{device_base_url}/api/v1/acquisition/stop", timeout=timeout)

    # 3. Capture fresh single frame on demand
    cap_resp = requests.post(f"{device_base_url}/api/v1/frame/capture", timeout=timeout)
    assert cap_resp.status_code == 200

    frame = cap_resp.json()
    assert frame["mode"] == "single"
    assert frame["width"] == exp["width"]
    assert frame["height"] == exp["height"]
    assert len(frame["pixels"]) == exp["width"] * exp["height"]

    # Temperature bounds check
    assert exp["min_valid_temp_c"] <= frame["min_c"] <= exp["max_valid_temp_c"]
    assert exp["min_valid_temp_c"] <= frame["max_c"] <= exp["max_valid_temp_c"]
    assert exp["min_valid_temp_c"] <= frame["ambient_c"] <= exp["max_valid_temp_c"]
    for px in frame["pixels"]:
        assert exp["min_valid_temp_c"] <= px <= exp["max_valid_temp_c"]

    # 4. Resume continuous mode
    requests.post(f"{device_base_url}/api/v1/acquisition/start", timeout=timeout)

def test_get_cached_frame(device_base_url, test_config):
    url = f"{device_base_url}/api/v1/frame"
    resp = requests.get(url, timeout=test_config["http_timeout_sec"])
    assert resp.status_code == 200

    exp = test_config["expectations"]
    frame = resp.json()
    assert "seq" in frame
    assert "pixels" in frame
    assert frame["width"] == exp["width"]
    assert frame["height"] == exp["height"]
    assert len(frame["pixels"]) == exp["width"] * exp["height"]

def test_websocket_stream(ws_stream_url, test_config):
    exp = test_config["expectations"]
    ws = websocket.create_connection(ws_stream_url, timeout=5.0)
    frames_received = []

    for _ in range(3):
        msg = ws.recv()
        data = json.loads(msg)
        assert "seq" in data
        assert "pixels" in data
        assert data["width"] == exp["width"]
        assert data["height"] == exp["height"]
        assert len(data["pixels"]) == exp["width"] * exp["height"]
        frames_received.append(data)

    ws.close()
    assert len(frames_received) >= 2
    # Verify advancing sequence numbers
    assert frames_received[1]["seq"] > frames_received[0]["seq"]
