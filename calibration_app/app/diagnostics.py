"""Small Android logcat diagnostic adapter."""

import logging


def _android_info(tag: str, message: str) -> None:
    from jnius import autoclass

    autoclass("android.util.Log").i(tag, message)


def diagnose(message: str) -> None:
    try:
        _android_info("CaneDiag", message)
    except Exception:
        logging.getLogger("CaneDiag").info(message)
