from app.controller import CalibrationController
from app.protocol import FrameParser, encode_frame
import app.controller as controller_module


def deliver(controller, parser, payload):
    for packet in parser.feed(encode_frame(0x07, payload)):
        controller.handle_packet(packet)


def test_firmware_log_text_reaches_diagnostics_without_bad_packets(monkeypatch):
    messages = []
    monkeypatch.setattr(controller_module, "diagnose", messages.append, raising=False)
    controller, parser = CalibrationController(), FrameParser()
    deliver(controller, parser, b"I (123) BT: connected\n")
    assert messages == ["Firmware: I (123) BT: connected\n"]
    assert parser.bad_packet_count == controller.bad_payload_count == 0


def test_log_utf8_can_span_packets_and_invalid_bytes_are_replaced(monkeypatch):
    messages = []
    monkeypatch.setattr(controller_module, "diagnose", messages.append, raising=False)
    controller, parser = CalibrationController(), FrameParser()
    deliver(controller, parser, b"caf\xc3")
    deliver(controller, parser, b"\xa9\xff\n")
    assert "".join(message.removeprefix("Firmware: ") for message in messages) == "café�\n"
    assert parser.bad_packet_count == controller.bad_payload_count == 0


def test_reset_discards_an_incomplete_log_character(monkeypatch):
    messages = []
    monkeypatch.setattr(controller_module, "diagnose", messages.append, raising=False)
    controller, parser = CalibrationController(), FrameParser()
    deliver(controller, parser, b"\xc3")
    controller.reset_session()
    deliver(controller, parser, b"ok")
    assert messages == ["Firmware: ok"]


def test_diagnostic_failure_does_not_reject_log_or_following_status(monkeypatch):
    def broken(_message):
        raise RuntimeError("diagnostic sink unavailable")

    monkeypatch.setattr(controller_module, "diagnose", broken, raising=False)
    controller, parser = CalibrationController(), FrameParser()
    deliver(controller, parser, b"hello")
    import struct
    for packet in parser.feed(encode_frame(0x03, struct.pack("<IIII", 10, 10, 0, 1000))):
        controller.handle_packet(packet)
    assert controller.firmware_status.samples_transmitted == 10
    assert parser.bad_packet_count == controller.bad_payload_count == 0
