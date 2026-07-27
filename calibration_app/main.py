from kivy.app import App
from kivy.clock import Clock
from kivy.lang import Builder
from kivy.properties import StringProperty
from kivy.uix.boxlayout import BoxLayout

from app.controller import CalibrationController
from app.kivy_graph import CalibrationGraph
from app.reader import CalibrationReader
from app.serial_backends.windows_serial import WindowsSerialBackend


class CalibrationRoot(BoxLayout):
    device_text = StringProperty("No device selected")
    status_text = StringProperty("Disconnected")

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.backend = WindowsSerialBackend()
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
            self.backend.connect(self._device_id, 921600)
            self.reader.start()
            self.status_text = "Connected — waiting for metadata"

    def refresh(self, _dt):
        config = self.controller.model.graph_config
        if config is not None:
            self.status_text = f"{config.axis_label} axis, ±{config.range_g} g, drops {self.controller.firmware_status.samples_dropped}, gaps {self.controller.model.host_sequence_gaps}"
            self.ids.graph.render(self.controller.model)


class CalibrationApp(App):
    def build(self):
        return Builder.load_file("calibration.kv")


if __name__ == "__main__":
    CalibrationApp().run()
