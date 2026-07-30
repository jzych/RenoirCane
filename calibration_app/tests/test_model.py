import struct

import pytest

from app.model import CalibrationModel, Sample


def metadata(rate: int, range_g: int, axis: int, threshold: int, sensitivity: float) -> bytes:
    return struct.pack("<HBBhfBBBB", rate, range_g, axis, threshold, sensitivity, 14, 4, 1, 0)


def test_metadata_configures_history_axis_range_and_thresholds():
    model = CalibrationModel()
    model.apply_metadata(metadata(400, 4, 0, 1024, 0.976))

    assert model.graph_config.axis_label == "X"
    assert model.graph_config.range_g == 4
    assert model.graph_config.capacity == 12000
    assert model.graph_config.threshold_g == pytest.approx(0.999424)

    model.apply_metadata(metadata(800, 16, 2, 4096, 1.952))
    assert model.graph_config.axis_label == "Z"
    assert model.graph_config.range_g == 16
    assert model.graph_config.capacity == 24000
    assert model.graph_config.threshold_g == pytest.approx(7.995392)


def test_metadata_adds_twenty_percent_graph_headroom():
    model = CalibrationModel()
    model.apply_metadata(metadata(800, 10, 1, 6000, 0.976))

    assert model.graph_config.display_range_g == pytest.approx(12.0)


def test_sequence_gap_and_ring_capacity_are_metadata_driven():
    model = CalibrationModel()
    model.apply_metadata(metadata(1, 16, 1, 6000, 0.976))
    model.append_samples([Sample(1, 1, 1, 2, 3), Sample(3, 2, 4, 5, 6)] + [Sample(seq, seq, seq, seq, seq) for seq in range(4, 33)])

    assert model.host_sequence_gaps == 1
    assert len(model.samples) == 30
    assert model.samples[0].sequence == 3
    assert model.samples[-1].sequence == 32


def test_graph_snapshot_copies_config_samples_events_and_gap_count():
    model = CalibrationModel()
    model.apply_metadata(metadata(1, 16, 1, 6000, 0.976))
    model.append_samples([Sample(1, 10, 1, 2, 3), Sample(3, 20, 4, 5, 6)])

    config, samples, impact_events, gaps = model.graph_snapshot()
    model.append_samples([Sample(4, 30, 7, 8, 9)])

    assert config.axis_label == "Y"
    assert [sample.sequence for sample in samples] == [1, 3]
    assert impact_events == ()
    assert gaps == 1


def test_graph_snapshot_expires_impact_events_outside_the_sample_window():
    model = CalibrationModel()
    model.apply_metadata(metadata(1, 16, 1, 6000, 0.976))
    model.record_impact_event(0, "processed")
    model.append_samples([Sample(index, index * 1_000_000, 0, 0, 0) for index in range(31)])
    model.record_impact_event(30_000_000, "skipped")

    _config, _samples, impact_events, _gaps = model.graph_snapshot()

    assert [(event.timestamp_us, event.outcome) for event in impact_events] == [
        (30_000_000, "skipped"),
    ]


def test_graph_snapshot_keeps_a_newer_impact_until_samples_reach_its_timestamp():
    model = CalibrationModel()
    model.apply_metadata(metadata(1, 16, 1, 6000, 0.976))
    model.append_samples([Sample(1, 1_000_000, 0, 0, 0)])

    model.record_impact_event(1_500_000, "processed")

    _config, _samples, impact_events, _gaps = model.graph_snapshot()
    assert [(event.timestamp_us, event.outcome) for event in impact_events] == [
        (1_500_000, "processed"),
    ]


def test_graph_snapshot_expires_events_outside_a_window_that_crosses_timestamp_wrap():
    model = CalibrationModel()
    model.apply_metadata(metadata(1, 16, 1, 6000, 0.976))
    start_timestamp_us = 0xFFF00000
    model.record_impact_event(start_timestamp_us, "processed")
    model.append_samples([
        Sample(index, (start_timestamp_us + index * 1_000_000) & 0xFFFFFFFF, 0, 0, 0)
        for index in range(31)
    ])

    _config, _samples, impact_events, _gaps = model.graph_snapshot()

    assert impact_events == ()
