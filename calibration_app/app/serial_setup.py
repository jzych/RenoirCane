from collections.abc import Callable
import time
from typing import Protocol


class ResettableSerialPort(Protocol):
    dtr: bool
    rts: bool

    def reset_input_buffer(self) -> None: ...


def reset_calibration_transport(
    port: ResettableSerialPort,
    *,
    sleep: Callable[[float], None] = time.sleep,
) -> None:
    """Reset the device, then discard bytes produced before the reset settled."""
    port.dtr = False
    port.rts = True
    sleep(0.1)
    port.rts = False
    port.dtr = True
    sleep(0.5)
    port.reset_input_buffer()
