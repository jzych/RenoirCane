from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import struct


@dataclass(frozen=True)
class Sample:
    sequence: int
    timestamp_us: int
    x_raw: int
    y_raw: int
    z_raw: int


@dataclass(frozen=True)
class GraphConfig:
    axis_label: str
    range_g: int
    capacity: int
    threshold_g: float


class CalibrationModel:
    def __init__(self) -> None:
        self.samples: deque[Sample] = deque()
        self.graph_config: GraphConfig | None = None
        self.host_sequence_gaps = 0
        self._expected_sequence: int | None = None

    def apply_metadata(self, payload: bytes) -> None:
        rate, range_g, axis, threshold, sensitivity, _int1, _int2, _format, _reserved = struct.unpack("<HBBhfBBBB", payload)
        axis_label = ("X", "Y", "Z")[axis]
        capacity = round(rate * 30)
        self.graph_config = GraphConfig(axis_label, range_g, capacity, abs(threshold) * sensitivity / 1000.0)
        self.samples = deque(self.samples, maxlen=capacity)

    def append_samples(self, samples: list[Sample]) -> None:
        for sample in samples:
            if self._expected_sequence is not None and sample.sequence != self._expected_sequence:
                self.host_sequence_gaps += (sample.sequence - self._expected_sequence) & 0xFFFFFFFF
            self._expected_sequence = (sample.sequence + 1) & 0xFFFFFFFF
            self.samples.append(sample)
