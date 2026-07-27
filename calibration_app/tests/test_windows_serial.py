from unittest.mock import MagicMock, patch

from app.serial_backends.windows_serial import WindowsSerialBackend


def test_windows_backend_enumerates_and_opens_ch9102_at_calibration_rate():
    port_info = MagicMock(device="COM4", description="USB-Enhanced-SERIAL CH9102")
    backend = WindowsSerialBackend()

    with patch("app.serial_backends.windows_serial.list_ports.comports", return_value=[port_info]):
        assert backend.list_devices()[0].label == "COM4 — USB-Enhanced-SERIAL CH9102"

    with patch("app.serial_backends.windows_serial.serial.Serial") as serial_port:
        backend.connect("COM4", 921600)

    serial_port.assert_called_once_with(port="COM4", baudrate=921600, bytesize=8, parity="N", stopbits=1, timeout=0.1)
