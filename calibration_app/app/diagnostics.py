"""Small Android logcat diagnostic adapter."""

import logging


def _android_info(tag: str, message: str) -> None:
    from jnius import autoclass

    autoclass("android.util.Log").i(tag, message)


def diagnose(message: str) -> None:
    """Send best-effort diagnostics without interrupting the caller's work."""
    try:
        _android_info("CaneDiag", message)
    except Exception:
        try:
            logging.getLogger("CaneDiag").info(message)
        except Exception:
            # Neither sink is usable; reporting this failure could recurse.
            pass
