import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import MagicMock, patch

from main import CalibrationApp, CalibrationRoot
from app.model import GraphConfig, Sample
from app.protocol import CAL_CMD_GET_METADATA, encode_frame
from app.serial_backends.base import SerialDevice


class FakeBackend:
    def __init__(self):
        self.connected = False
        self.writes = []

    def list_devices(self):
        return [
            SerialDevice("COM3", "COM3 — Bluetooth link"),
            SerialDevice("COM4", "COM4 — USB-Enhanced-SERIAL CH9102"),
        ]

    def connect(self, device_id, baudrate):
        self.connected = True

    def write(self, data):
        self.writes.append(data)


class GraphRecorder:
    def render(self, config, samples):
        self.received = config, samples


class CalibrationAppTests(TestCase):
    def test_main_imports_without_windows_serial_module(self):
        script = (
            "import sys; "
            "sys.modules['app.serial_backends.windows_serial'] = None; "
            "import main"
        )

        result = subprocess.run(
            [sys.executable, "-c", script],
            cwd=Path(__file__).resolve().parents[1],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_build_loads_kv_rule_and_returns_calibration_root(self):
        app = CalibrationApp()

        with patch("main.Builder.load_file") as load_file:
            root = app.build()

        load_file.assert_called_once()
        self.assertIsInstance(root, CalibrationRoot)

    def test_load_kv_does_not_automatically_load_the_rule_a_second_time(self):
        app = CalibrationApp()

        with patch("main.App.load_kv") as automatic_load:
            app.load_kv()

        automatic_load.assert_not_called()

    def test_refresh_exposes_all_devices_and_selects_the_requested_port(self):
        root = CalibrationRoot()
        root.backend = FakeBackend()

        root.refresh_devices()
        root.select_device("COM4 — USB-Enhanced-SERIAL CH9102")

        self.assertEqual(root.device_labels, [
            "COM3 — Bluetooth link",
            "COM4 — USB-Enhanced-SERIAL CH9102",
        ])
        self.assertEqual(root.device_text, "COM4 — USB-Enhanced-SERIAL CH9102")
        self.assertEqual(root._device_id, "COM4")

    def test_connect_requests_metadata_from_the_firmware(self):
        root = CalibrationRoot()
        backend = FakeBackend()
        root.backend = backend
        root.reader = MagicMock()
        root.refresh_devices()

        root.connect_or_disconnect()

        self.assertEqual(backend.writes, [encode_frame(CAL_CMD_GET_METADATA, b"")])
        root.reader.start.assert_called_once_with()

    def test_refresh_renders_the_model_snapshot_not_live_samples(self):
        config = GraphConfig("Y", 4, 12000, 1.0, 0.976)
        samples = (Sample(1, 10, 1, 2, 3),)
        root = CalibrationRoot()
        root.reader = SimpleNamespace(error=None)
        root.controller.model = SimpleNamespace(
            graph_snapshot=lambda: (config, samples, 7)
        )
        root.controller.firmware_status = SimpleNamespace(samples_dropped=2)
        recorder = GraphRecorder()
        root.ids["graph"] = recorder

        root.refresh(0)

        self.assertEqual(recorder.received, (config, samples))
        self.assertTrue(root.status_text.endswith("gaps 7"))
