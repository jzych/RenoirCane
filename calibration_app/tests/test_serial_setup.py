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


class RecordingPort:
    def __init__(self, events: list) -> None:
        object.__setattr__(self, "events", events)

    def __setattr__(self, name, value) -> None:
        self.events.append((name, value))

    def reset_input_buffer(self) -> None:
        self.events.append(("flush",))


def test_reset_pulses_rts_then_flushes_only_after_the_boot_settles():
    events: list = []

    reset_calibration_transport(RecordingPort(events), sleep=lambda seconds: events.append(("sleep", seconds)))

    assert events == [
        ("dtr", False),
        ("rts", True),   # hold EN low
        ("sleep", 0.1),
        ("rts", False),  # release reset
        ("dtr", True),
        ("sleep", 0.5),  # boot output arrives here
        ("flush",),      # and is discarded
    ]
