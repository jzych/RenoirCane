from collections.abc import Callable
from typing import Any


def make_serial_backend(
    platform_name: str,
    *,
    android_factory: Callable[[], Any],
    windows_factory: Callable[[], Any],
) -> Any:
    return android_factory() if platform_name == "android" else windows_factory()
