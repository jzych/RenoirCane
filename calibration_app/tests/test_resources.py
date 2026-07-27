import sys

from app.resources import asset_path


def test_asset_path_uses_pyinstaller_bundle_directory(monkeypatch, tmp_path):
    monkeypatch.setattr(sys, "_MEIPASS", str(tmp_path), raising=False)

    assert asset_path("calibration.kv") == tmp_path / "calibration.kv"
