from unittest import TestCase
from unittest.mock import patch

from main import CalibrationApp, CalibrationRoot
from app.serial_backends.base import SerialDevice


class FakeBackend:
    connected = False

    def list_devices(self):
        return [
            SerialDevice("COM3", "COM3 — Bluetooth link"),
            SerialDevice("COM4", "COM4 — USB-Enhanced-SERIAL CH9102"),
        ]


class CalibrationAppTests(TestCase):
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
