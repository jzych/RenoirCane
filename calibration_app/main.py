from kivy.app import App
from kivy.clock import Clock
from kivy.lang import Builder
from kivy.properties import ListProperty, StringProperty
from kivy.uix.boxlayout import BoxLayout
from kivy.utils import platform

from app.controller import CalibrationController
from app.kivy_graph import CalibrationGraph
from app.platform import make_serial_backend
from app.protocol import CAL_CMD_GET_METADATA, encode_frame
from app.reader import CalibrationReader
from app.resources import asset_path
from app.serial_backends.android_usb_serial import UsbPermissionPendingError


def _make_android_backend():
    from app.serial_backends.android_usb_serial import AndroidUsbSerialBackend

    return AndroidUsbSerialBackend()


def _make_windows_backend():
    from app.serial_backends.windows_serial import WindowsSerialBackend

    return WindowsSerialBackend()


class CalibrationRoot(BoxLayout):
    device_text = StringProperty("No device selected")
    device_labels = ListProperty()
    status_text = StringProperty("Disconnected")

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.backend = make_serial_backend(
            platform,
            android_factory=_make_android_backend,
            windows_factory=_make_windows_backend,
        )
        self.controller = CalibrationController()
        self.reader = CalibrationReader(self.backend, self.controller)
        Clock.schedule_interval(self.refresh, 1 / 20)

    def refresh_devices(self):
        devices = self.backend.list_devices()
        self._device_ids = {device.label: device.id for device in devices}
        self.device_labels = list(self._device_ids)
        if not self.device_labels:
            self.device_text = "No serial devices found"
            self._device_id = None
            return
        selected = self.device_text if self.device_text in self._device_ids else self.device_labels[0]
        self.select_device(selected)

    def select_device(self, label: str) -> None:
        device_id = getattr(self, "_device_ids", {}).get(label)
        if device_id is not None:
            self.device_text = label
            self._device_id = device_id

    def connect_or_disconnect(self):
        if self.backend.connected:
            self.reader.stop(); self.backend.close(); self.status_text = "Disconnected"; return
        if getattr(self, "_device_id", None) is None:
            self.refresh_devices()
        if self._device_id is not None:
            try:
                self.backend.connect(self._device_id, 921600)
                self.controller.reset_session()
                self.backend.write(encode_frame(CAL_CMD_GET_METADATA, b""))
                self.reader.start()
            except UsbPermissionPendingError:
                self.status_text = "USB permission requested; accept it, then connect again"
                return
            except Exception as error:
                self.backend.close()
                self.status_text = f"Connection failed: {error}"
                return
            self.status_text = "Connected — waiting for metadata"

    def refresh(self, _dt):
        if self.reader.error is not None:
            self.status_text = f"Disconnected: {self.reader.error}"
            return
        config, samples, impact_events, host_sequence_gaps = self.controller.model.graph_snapshot()
        if config is not None:
            telemetry = self.controller.impact_telemetry
            self.status_text = (
                f"{config.axis_label} axis, graph ±{config.display_range_g:g} g, "
                f"drops {self.controller.firmware_status.samples_dropped}, gaps {host_sequence_gaps}; "
                f"irq {telemetry.int1_received}, tap {telemetry.tap_confirmed}, "
                f"threshold {telemetry.threshold_rejected}, refractory {telemetry.refractory_skipped}, "
                f"busy {telemetry.audio_busy_skipped}, sent {telemetry.event_packets_sent}, "
                f"queue {telemetry.event_queue_dropped}"
            )
            self.ids.graph.render(config, samples, impact_events)


class CalibrationApp(App):
    def load_kv(self, filename=None):
        """Load the shared KV rule once from the bundle-aware build method."""
        return None

    def build(self):
        Builder.load_file(str(asset_path("calibration.kv")))
        return CalibrationRoot()


if __name__ == "__main__":
    CalibrationApp().run()
