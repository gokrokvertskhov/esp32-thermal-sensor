import time

def test_serial_help(serial_conn):
    lines = serial_conn.send_cmd("help")
    output = " ".join(lines)
    assert "i2c_scan" in output
    assert "heap" in output
    assert "status" in output
    assert "acq" in output
    assert "capture" in output

def test_serial_heap(serial_conn):
    lines = serial_conn.send_cmd("heap")
    output = "\n".join(lines)
    assert "Internal DRAM" in output
    assert "Octal PSRAM" in output

def test_serial_i2c_scan(serial_conn):
    lines = serial_conn.send_cmd("i2c_scan", wait_sec=1.5)
    output = "\n".join(lines)
    assert "Scanning shared I2C bus" in output
    # Verify expected hardware devices are present if connected
    # 0x20: TCA9554 IO Expander, 0x33: MLX90640, 0x51: PCF85063 RTC
    assert "0x51" in output or "PCF85063" in output or "0x20" in output

def test_serial_status(serial_conn):
    lines = serial_conn.send_cmd("status")
    output = "\n".join(lines)
    assert "Device Status" in output
    assert "Wi-Fi Connected" in output
    assert "RTC / System ISO" in output
