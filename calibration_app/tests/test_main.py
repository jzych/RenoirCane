import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import MagicMock, patch

from main import CalibrationApp, CalibrationRoot
from app.model import GraphConfig, ImpactEvent, Sample
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
    def render(self, config, samples, impact_events):
        self.received = config, samples, impact_events


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

    def _root_with_snapshot(self, config, samples=(), impact_events=(), gaps=0, reader_error=None):
        root = CalibrationRoot()
        root.reader = SimpleNamespace(error=reader_error)
        root.controller.model = SimpleNamespace(
            graph_snapshot=lambda: (config, samples, impact_events, gaps)
        )
        root.controller.firmware_status = SimpleNamespace(samples_dropped=2)
        root.controller.impact_telemetry = SimpleNamespace(
            int1_received=1,
            tap_confirmed=2,
            threshold_rejected=3,
            refractory_skipped=4,
            audio_busy_skipped=5,
            event_packets_sent=6,
            event_queue_dropped=7,
        )
        recorder = GraphRecorder()
        root.ids["graph"] = recorder
        return root, recorder

    def test_refresh_renders_the_model_snapshot_not_live_samples(self):
        config = GraphConfig("Y", 4, 12000, 1.0, 0.976)
        samples = (Sample(1, 10, 1, 2, 3),)
        impact_events = (ImpactEvent(10, "processed"),)
        root, recorder = self._root_with_snapshot(config, samples, impact_events, gaps=7)

        root.refresh(0)

        self.assertEqual(recorder.received, (config, samples, impact_events))
        self.assertEqual(
            root.status_text,
            "Y axis, graph ±4.8 g, drops 2, gaps 7; "
            "irq 1, tap 2, threshold 3, refractory 4, busy 5, sent 6, queue 7",
        )

    def test_refresh_reports_reader_error_without_rendering(self):
        config = GraphConfig("Y", 4, 12000, 1.0, 0.976)
        root, recorder = self._root_with_snapshot(config, reader_error="port closed")

        root.refresh(0)

        self.assertEqual(root.status_text, "Disconnected: port closed")
        self.assertFalse(hasattr(recorder, "received"))

    def test_refresh_waits_for_metadata_before_rendering(self):
        root, recorder = self._root_with_snapshot(None)
        root.status_text = "Connected — waiting for metadata"

        root.refresh(0)

        self.assertEqual(root.status_text, "Connected — waiting for metadata")
        self.assertFalse(hasattr(recorder, "received"))
