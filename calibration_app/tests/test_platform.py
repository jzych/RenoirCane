from app.platform import make_serial_backend


def test_selects_android_backend_factory_on_android():
    selected = make_serial_backend(
        "android",
        android_factory=lambda: "android backend",
        windows_factory=lambda: "windows backend",
    )

    assert selected == "android backend"


def test_selects_windows_backend_factory_off_android():
    selected = make_serial_backend(
        "win",
        android_factory=lambda: "android backend",
        windows_factory=lambda: "windows backend",
    )

    assert selected == "windows backend"
