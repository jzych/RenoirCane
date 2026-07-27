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


def test_sequence_gap_and_ring_capacity_are_metadata_driven():
    model = CalibrationModel()
    model.apply_metadata(metadata(1, 16, 1, 6000, 0.976))
    model.append_samples([Sample(1, 1, 1, 2, 3), Sample(3, 2, 4, 5, 6)] + [Sample(seq, seq, seq, seq, seq) for seq in range(4, 33)])

    assert model.host_sequence_gaps == 1
    assert len(model.samples) == 30
    assert model.samples[0].sequence == 3
    assert model.samples[-1].sequence == 32
