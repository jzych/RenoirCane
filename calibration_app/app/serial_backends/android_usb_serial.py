"""Android USB-host serial backend.

The PyJNIus bridge is kept behind a small adapter so the backend's connection
state can be exercised on Windows without an Android runtime.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from app.serial_backends.base import SerialDevice


class UsbPermissionPendingError(RuntimeError):
    """The Android permission dialog was requested; retry after the response."""



class AndroidUsbUnavailableError(RuntimeError):
    """The USB backend was used outside an Android python-for-android app."""


@dataclass(frozen=True)
class _AndroidDriver:
    raw: Any
    device_id: int
    label: str


class _AndroidPort:
    READ_TIMEOUT_MS = 100
    WRITE_TIMEOUT_MS = 1_000

    def __init__(self, raw: Any) -> None:
        self._raw = raw

    def read(self, size: int) -> bytes:
        buffer = bytearray(size)
        received = self._raw.read(buffer, self.READ_TIMEOUT_MS)
        return bytes(buffer[:received]) if received > 0 else b""

    def write(self, data: bytes) -> None:
        self._raw.write(data, self.WRITE_TIMEOUT_MS)

    def close(self) -> None:
        self._raw.close()


class _PyjniusAndroidUsbBridge:
    def __init__(self) -> None:
        try:
            from android import mActivity
            from jnius import autoclass
        except ImportError as error:
            raise AndroidUsbUnavailableError(
                "Android USB serial requires an Android runtime"
            ) from error

        context = autoclass("android.content.Context")
        self._activity = mActivity
        self._manager = mActivity.getSystemService(context.USB_SERVICE)
        self._intent = autoclass("android.content.Intent")
        self._pending_intent = autoclass("android.app.PendingIntent")
        self._prober = autoclass(
            "com.hoho.android.usbserial.driver.UsbSerialProber"
        )
        self._serial_port = autoclass(
            "com.hoho.android.usbserial.driver.UsbSerialPort"
        )
        self._permission_action = f"{mActivity.getPackageName()}.USB_PERMISSION"

    def probe_drivers(self) -> list[_AndroidDriver]:
        drivers = self._prober.getDefaultProber().findAllDrivers(self._manager)
        return [self._describe(driver) for driver in drivers]

    def has_permission(self, driver: _AndroidDriver) -> bool:
        return bool(self._manager.hasPermission(driver.raw.getDevice()))

    def request_permission(self, driver: _AndroidDriver) -> None:
        intent = self._intent(self._permission_action)
        pending = self._pending_intent.getBroadcast(
            self._activity,
            driver.device_id,
            intent,
            self._pending_intent.FLAG_IMMUTABLE,
        )
        self._manager.requestPermission(driver.raw.getDevice(), pending)

    def open(self, driver: _AndroidDriver, baudrate: int) -> _AndroidPort:
        connection = self._manager.openDevice(driver.raw.getDevice())
        if connection is None:
            raise PermissionError("USB permission was denied or the device detached")
        port = driver.raw.getPorts().get(0)
        try:
            port.open(connection)
            port.setParameters(
                baudrate,
                8,
                self._serial_port.STOPBITS_1,
                self._serial_port.PARITY_NONE,
            )
        except Exception:
            port.close()
            raise
        return _AndroidPort(port)

    @staticmethod
    def _describe(raw_driver: Any) -> _AndroidDriver:
        device = raw_driver.getDevice()
        label = (
            f"{raw_driver.getClass().getSimpleName()} — USB "
            f"{device.getVendorId():04x}:{device.getProductId():04x}"
        )
        return _AndroidDriver(raw_driver, int(device.getDeviceId()), label)


class AndroidUsbSerialBackend:
    def __init__(self, bridge: Any | None = None) -> None:
        self._bridge = bridge
        self._drivers: dict[str, Any] = {}
        self._port: Any | None = None

    def list_devices(self) -> list[SerialDevice]:
        drivers = self._require_bridge().probe_drivers()
        self._drivers = {str(driver.device_id): driver for driver in drivers}
        return [
            SerialDevice(str(driver.device_id), driver.label)
            for driver in drivers
        ]

    def connect(self, device_id: str, baudrate: int) -> None:
        driver = self._drivers.get(device_id)
        if driver is None:
            self.list_devices()
            driver = self._drivers.get(device_id)
        if driver is None:
            raise ValueError(f"USB serial device {device_id} is no longer attached")
        bridge = self._require_bridge()
        if not bridge.has_permission(driver):
            bridge.request_permission(driver)
            raise UsbPermissionPendingError("USB permission requested; connect again after responding")
        self._port = bridge.open(driver, baudrate)

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
        return self._port is not None

    def _require_port(self) -> Any:
        if self._port is None:
            raise RuntimeError("USB serial device is not connected")
        return self._port

    def _require_bridge(self) -> Any:
        if self._bridge is None:
            self._bridge = _PyjniusAndroidUsbBridge()
        return self._bridge
