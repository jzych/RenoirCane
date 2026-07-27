import threading

from app.controller import CalibrationController
from app.reader import CalibrationReader


class FailingBackend:
    def __init__(self) -> None:
        self._connected = True
        self.closed = False
        self.read_started = threading.Event()

    @property
    def connected(self) -> bool:
        return self._connected

    def read(self, _size: int) -> bytes:
        self.read_started.set()
        raise OSError("USB device detached")

    def close(self) -> None:
        self.closed = True
        self._connected = False


def test_reader_closes_backend_when_serial_read_fails():
    backend = FailingBackend()
    reader = CalibrationReader(backend, CalibrationController())

    reader.start()
    assert backend.read_started.wait(timeout=1.0)
    reader.stop()

    assert backend.closed is True
    assert reader.error == "USB device detached"
