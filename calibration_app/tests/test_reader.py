import struct
import threading

import app.diagnostics as diagnostics_module
import app.reader as reader_module
from app.controller import CalibrationController
from app.protocol import CAL_PKT_METADATA, encode_frame
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


class MetadataFrameBackend:
    def __init__(self) -> None:
        self._connected = True
        self.read_called = threading.Event()

    @property
    def connected(self) -> bool:
        return self._connected

    def read(self, _size: int) -> bytes:
        self._connected = False
        self.read_called.set()
        return encode_frame(CAL_PKT_METADATA, b"")

    def close(self) -> None:
        self._connected = False


def test_reader_closes_backend_when_serial_read_fails():
    backend = FailingBackend()
    reader = CalibrationReader(backend, CalibrationController())

    reader.start()
    assert backend.read_started.wait(timeout=1.0)
    reader.stop()

    assert backend.closed is True
    assert reader.error == "USB device detached"


def test_reader_logs_decoded_packet_count(monkeypatch):
    messages = []
    monkeypatch.setattr(reader_module, "diagnose", messages.append)
    backend = MetadataFrameBackend()
    reader = CalibrationReader(backend, CalibrationController())

    reader.start()
    assert backend.read_called.wait(timeout=1.0)
    reader.stop()

    assert "Reader decoded packets=1" in messages


class PacketStreamBackend:
    def __init__(self, data: bytes) -> None:
        self._data = data
        self._connected = True
        self.drained = threading.Event()

    @property
    def connected(self) -> bool:
        return self._connected

    def read(self, _size: int) -> bytes:
        self._connected = False
        self.drained.set()
        return self._data

    def close(self) -> None:
        self._connected = False


def test_reader_forwards_decoded_packets_to_the_controller_and_counts_bad_frames(monkeypatch):
    messages = []
    monkeypatch.setattr(reader_module, "diagnose", messages.append)
    metadata = struct.pack("<HBBhfBBBB", 800, 16, 1, 4500, 0.488, 14, 4, 1, 0)
    corrupt = bytearray(encode_frame(CAL_PKT_METADATA, metadata))
    corrupt[5] ^= 0x01
    backend = PacketStreamBackend(bytes(corrupt) + encode_frame(CAL_PKT_METADATA, metadata))
    controller = CalibrationController()
    reader = CalibrationReader(backend, controller)

    reader.start()
    assert backend.drained.wait(timeout=1.0)
    reader.stop()

    assert reader.error is None
    assert controller.model.graph_config.axis_label == "Y"
    assert "Reader bad_packets=1" in messages


def test_reader_survives_platform_diagnostics(monkeypatch):
    monkeypatch.setattr(
        diagnostics_module,
        "_android_info",
        lambda _tag, _message: (_ for _ in ()).throw(Exception("Unable to find JAVA_HOME")),
    )
    metadata = struct.pack("<HBBhfBBBB", 800, 16, 1, 4500, 0.488, 14, 4, 1, 0)
    backend = PacketStreamBackend(encode_frame(CAL_PKT_METADATA, metadata))
    controller = CalibrationController()
    reader = CalibrationReader(backend, controller)

    reader.start()
    assert backend.drained.wait(timeout=1.0)
    reader.stop()

    assert reader.error is None
    assert controller.model.graph_config is not None
