from unittest import TestCase
from unittest.mock import patch

from main import CalibrationApp, CalibrationRoot


class CalibrationAppTests(TestCase):
    def test_build_loads_kv_rule_and_returns_calibration_root(self):
        app = CalibrationApp()

        with patch("main.Builder.load_file") as load_file:
            root = app.build()

        load_file.assert_called_once()
        self.assertIsInstance(root, CalibrationRoot)
