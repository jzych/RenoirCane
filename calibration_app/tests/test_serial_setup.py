from app.serial_setup import reset_calibration_transport


class FakePort:
    def __init__(self) -> None:
        self.dtr = None
        self.rts = None
        self.input_flushed = False

    def reset_input_buffer(self) -> None:
        self.input_flushed = True


def test_reset_calibration_transport_discards_pre_reset_packets():
    port = FakePort()
    delays: list[float] = []

    reset_calibration_transport(port, sleep=delays.append)

    assert port.dtr is True
    assert port.rts is False
    assert delays == [0.1, 0.5]
    assert port.input_flushed is True
