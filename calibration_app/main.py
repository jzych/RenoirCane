from kivy.app import App
from kivy.clock import Clock
from kivy.lang import Builder
from kivy.properties import StringProperty
from kivy.uix.boxlayout import BoxLayout
from kivy.utils import platform

from app.controller import CalibrationController
from app.kivy_graph import CalibrationGraph
from app.platform import make_serial_backend
from app.reader import CalibrationReader
from app.resources import asset_path
from app.serial_backends.android_usb_serial import AndroidUsbSerialBackend, UsbPermissionPendingError
from app.serial_backends.windows_serial import WindowsSerialBackend


class CalibrationRoot(BoxLayout):
    device_text = StringProperty("No device selected")
    status_text = StringProperty("Disconnected")

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.backend = make_serial_backend(
            platform,
            android_factory=AndroidUsbSerialBackend,
            windows_factory=WindowsSerialBackend,
        )
        self.controller = CalibrationController()
        self.reader = CalibrationReader(self.backend, self.controller)
        Clock.schedule_interval(self.refresh, 1 / 20)

    def refresh_devices(self):
        devices = self.backend.list_devices()
        self.device_text = devices[0].label if devices else "No serial devices found"
        self._device_id = devices[0].id if devices else None

    def connect_or_disconnect(self):
        if self.backend.connected:
            self.reader.stop(); self.backend.close(); self.status_text = "Disconnected"; return
        if getattr(self, "_device_id", None) is None:
            self.refresh_devices()
        if self._device_id is not None:
            try:
                self.backend.connect(self._device_id, 921600)
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
        config = self.controller.model.graph_config
        if config is not None:
            self.status_text = f"{config.axis_label} axis, ±{config.range_g} g, drops {self.controller.firmware_status.samples_dropped}, gaps {self.controller.model.host_sequence_gaps}"
            self.ids.graph.render(self.controller.model)


class CalibrationApp(App):
    def build(self):
        Builder.load_file(str(asset_path("calibration.kv")))
        return CalibrationRoot()


if __name__ == "__main__":
    CalibrationApp().run()
