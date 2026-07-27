import serial
from serial.tools import list_ports

from app.serial_backends.base import SerialDevice


class WindowsSerialBackend:
    def __init__(self) -> None:
        self._port: serial.Serial | None = None

    def list_devices(self) -> list[SerialDevice]:
        return [SerialDevice(info.device, f"{info.device} — {info.description}") for info in list_ports.comports()]

    def connect(self, device_id: str, baudrate: int) -> None:
        self._port = serial.Serial(port=device_id, baudrate=baudrate, bytesize=8, parity="N", stopbits=1, timeout=0.1)

    def read(self, size: int) -> bytes:
        return self._require_port().read(size)

    def write(self, data: bytes) -> None:
        self._require_port().write(data)

    def close(self) -> None:
        if self._port is not None:
            self._port.close()
            self._port = None

    @property
    def connected(self) -> bool:
        return self._port is not None and self._port.is_open

    def _require_port(self) -> serial.Serial:
        if self._port is None:
            raise RuntimeError("serial port is not connected")
        return self._port
