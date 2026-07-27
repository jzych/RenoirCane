import struct

from app.controller import CalibrationController
from app.protocol import CAL_PKT_METADATA, CAL_PKT_SAMPLE_BATCH, CAL_PKT_STATUS, Packet


def test_controller_applies_metadata_samples_and_status():
    controller = CalibrationController()
    metadata = struct.pack("<HBBhfBBBB", 800, 16, 1, 6000, 0.976, 14, 4, 1, 0)
    samples = bytes([2]) + struct.pack("<IIhhhIIhhh", 10, 100, 1, 2, 3, 11, 200, 4, 5, 6)
    status = struct.pack("<IIII", 20, 18, 2, 3000)

    controller.handle_packet(Packet(1, CAL_PKT_METADATA, metadata))
    controller.handle_packet(Packet(1, CAL_PKT_SAMPLE_BATCH, samples))
    controller.handle_packet(Packet(1, CAL_PKT_STATUS, status))

    assert controller.model.graph_config.axis_label == "Y"
    assert [sample.sequence for sample in controller.model.samples] == [10, 11]
    assert controller.firmware_status.samples_dropped == 2


def test_controller_rejects_malformed_sample_batch():
    controller = CalibrationController()
    controller.handle_packet(Packet(1, CAL_PKT_SAMPLE_BATCH, b"\x02"))

    assert controller.bad_payload_count == 1
