from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import struct
import threading


@dataclass(frozen=True)
class Sample:
    sequence: int
    timestamp_us: int
    x_raw: int
    y_raw: int
    z_raw: int


@dataclass(frozen=True)
class ImpactEvent:
    timestamp_us: int
    outcome: str


@dataclass(frozen=True)
class GraphConfig:
    axis_label: str
    range_g: int
    capacity: int
    threshold_g: float
    sensitivity_mg_lsb: float

    @property
    def display_range_g(self) -> float:
        return self.range_g * 1.2


class CalibrationModel:
    def __init__(self) -> None:
        self.samples: deque[Sample] = deque()
        self.impact_events: deque[ImpactEvent] = deque()
        self.graph_config: GraphConfig | None = None
        self.host_sequence_gaps = 0
        self._expected_sequence: int | None = None
        self._lock = threading.Lock()

    def apply_metadata(self, payload: bytes) -> None:
        rate, range_g, axis, threshold, sensitivity, _int1, _int2, _format, _reserved = struct.unpack("<HBBhfBBBB", payload)
        axis_label = ("X", "Y", "Z")[axis]
        capacity = round(rate * 30)
        with self._lock:
            self.graph_config = GraphConfig(axis_label, range_g, capacity, abs(threshold) * sensitivity / 1000.0, sensitivity)
            self.samples = deque(self.samples, maxlen=capacity)

    def append_samples(self, samples: list[Sample]) -> None:
        with self._lock:
            for sample in samples:
                if self._expected_sequence is not None and sample.sequence != self._expected_sequence:
                    self.host_sequence_gaps += (sample.sequence - self._expected_sequence) & 0xFFFFFFFF
                self._expected_sequence = (sample.sequence + 1) & 0xFFFFFFFF
                self.samples.append(sample)
            self._expire_impact_events()

    def record_impact_event(self, timestamp_us: int, outcome: str) -> None:
        with self._lock:
            self.impact_events.append(ImpactEvent(timestamp_us, outcome))
            self._expire_impact_events()

    def graph_snapshot(self) -> tuple[GraphConfig | None, tuple[Sample, ...], tuple[ImpactEvent, ...], int]:
        with self._lock:
            self._expire_impact_events()
            return self.graph_config, tuple(self.samples), tuple(self.impact_events), self.host_sequence_gaps

    def _expire_impact_events(self) -> None:
        if not self.samples:
            return
        oldest_timestamp = self.samples[0].timestamp_us
        latest_timestamp = self.samples[-1].timestamp_us
        window_us = (latest_timestamp - oldest_timestamp) & 0xFFFFFFFF
        self.impact_events = deque(
            event
            for event in self.impact_events
            if self._event_is_visible_or_newer(event.timestamp_us, latest_timestamp, window_us)
        )

    @staticmethod
    def _event_is_visible_or_newer(timestamp_us: int, latest_timestamp_us: int, window_us: int) -> bool:
        age_us = (latest_timestamp_us - timestamp_us) & 0xFFFFFFFF
        return age_us <= window_us or age_us > 0x7FFFFFFF
