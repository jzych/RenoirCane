import struct

from app.controller import CalibrationController
from app.model import Sample
from app.protocol import (
    CAL_PKT_IMPACT_EVENT,
    CAL_PKT_METADATA,
    CAL_PKT_SAMPLE_BATCH,
    CAL_PKT_STATUS,
    Packet,
)


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


def test_controller_records_processed_impact_events_for_the_graph():
    controller = CalibrationController()

    controller.handle_packet(Packet(1, CAL_PKT_IMPACT_EVENT, struct.pack("<IB", 1_234_567, 1)))

    _config, _samples, impact_events, _gaps = controller.model.graph_snapshot()
    assert [(event.timestamp_us, event.outcome) for event in impact_events] == [
        (1_234_567, "processed"),
    ]


def test_controller_rejects_an_impact_event_with_an_unknown_outcome():
    controller = CalibrationController()

    controller.handle_packet(Packet(1, CAL_PKT_IMPACT_EVENT, struct.pack("<IB", 1_234_567, 99)))

    assert controller.bad_payload_count == 1


def test_controller_reset_session_discards_previous_samples_events_and_gaps():
    controller = CalibrationController()
    controller.model.apply_metadata(struct.pack("<HBBhfBBBB", 1, 16, 1, 6000, 0.976, 14, 4, 1, 0))
    controller.model.append_samples([Sample(1, 1, 0, 0, 0), Sample(3, 2, 0, 0, 0)])
    controller.model.record_impact_event(2, "processed")

    controller.reset_session()

    config, samples, impact_events, gaps = controller.model.graph_snapshot()
    assert config is None
    assert samples == ()
    assert impact_events == ()
    assert gaps == 0
