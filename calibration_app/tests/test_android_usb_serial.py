import pytest
import app.serial_backends.android_usb_serial as android_usb_serial

from app.serial_backends.android_usb_serial import (
    AndroidUsbSerialBackend,
    AndroidUsbUnavailableError,
    UsbPermissionPendingError,
    _AndroidPort,
    _PyjniusAndroidUsbBridge,
)
from app.serial_backends.base import SerialDevice


class FakeDriver:
    def __init__(self, device_id: int, label: str) -> None:
        self.device_id = device_id
        self.label = label


class FakeBridge:
    def __init__(self, permitted: bool = False) -> None:
        self.permission_requested_for = None
        self.permitted = permitted
        self.opened_with = None
        self.port = FakePort()

    def probe_drivers(self):
        return [FakeDriver(42, "CH9102F (1a86:55d4)")]

    def has_permission(self, _driver):
        return self.permitted

    def request_permission(self, driver):
        self.permission_requested_for = driver.device_id

    def open(self, driver, baudrate):
        self.opened_with = (driver.device_id, baudrate)
        return self.port


class FakePort:
    def __init__(self) -> None:
        self.written = []
        self.closed = False

    def read(self, _size):
        return b"sample"

    def write(self, data):
        self.written.append(data)

    def close(self):
        self.closed = True


class DescriptorOnlyDevice:
    def getDeviceId(self):
        return 42

    def getVendorId(self):
        return 0x1A86

    def getProductId(self):
        return 0x55D4


class DescriptorOnlyDriver:
    def getDevice(self):
        return DescriptorOnlyDevice()


class RawPortWithBytes:
    def read(self, buffer, _timeout):
        buffer[:3] = b"\x01\x02\x03"
        return 3


def test_android_port_logs_nonempty_read_without_changing_returned_bytes(monkeypatch):
    messages = []
    monkeypatch.setattr(android_usb_serial, "diagnose", messages.append)

    assert _AndroidPort(RawPortWithBytes()).read(16) == b"\x01\x02\x03"
    assert messages == ["USB read bytes=3 hex=010203"]


def test_describe_android_driver_uses_usb_ids_without_class_name_access():
    driver = _PyjniusAndroidUsbBridge._describe(DescriptorOnlyDriver())

    assert driver.device_id == 42
    assert driver.label == "USB serial — 1a86:55d4"


def test_lists_probed_android_usb_serial_devices():
    backend = AndroidUsbSerialBackend(bridge=FakeBridge())

    assert backend.list_devices() == [
        SerialDevice("42", "CH9102F (1a86:55d4)"),
    ]


def test_connect_requests_android_usb_permission_before_opening():
    bridge = FakeBridge()
    backend = AndroidUsbSerialBackend(bridge=bridge)
    backend.list_devices()

    with pytest.raises(UsbPermissionPendingError):
        backend.connect("42", 921600)

    assert bridge.permission_requested_for == 42


def test_connects_authorized_android_usb_device_at_requested_baudrate():
    bridge = FakeBridge(permitted=True)
    backend = AndroidUsbSerialBackend(bridge=bridge)
    backend.list_devices()

    backend.connect("42", 921600)

    assert bridge.opened_with == (42, 921600)
    assert backend.connected is True
    assert backend.read(32) == b"sample"
    backend.write(b"command")
    assert bridge.port.written == [b"command"]
    backend.close()
    assert bridge.port.closed is True
    assert backend.connected is False


def test_default_backend_explains_that_android_runtime_is_required_on_windows():
    backend = AndroidUsbSerialBackend()

    with pytest.raises(AndroidUsbUnavailableError, match="Android runtime"):
        backend.list_devices()
