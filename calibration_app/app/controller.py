from __future__ import annotations

from dataclasses import dataclass
import struct

from app.model import CalibrationModel, Sample
from app.protocol import (
    CAL_IMPACT_PROCESSED,
    CAL_IMPACT_SKIPPED,
    CAL_PKT_IMPACT_EVENT,
    CAL_PKT_IMPACT_TELEMETRY,
    CAL_PKT_METADATA,
    CAL_PKT_SAMPLE_BATCH,
    CAL_PKT_STATUS,
    Packet,
)


@dataclass(frozen=True)
class FirmwareStatus:
    samples_acquired: int = 0
    samples_transmitted: int = 0
    samples_dropped: int = 0
    uptime_ms: int = 0


@dataclass(frozen=True)
class ImpactTelemetry:
    int1_received: int = 0
    tap_confirmed: int = 0
    threshold_rejected: int = 0
    refractory_skipped: int = 0
    audio_busy_skipped: int = 0
    event_packets_sent: int = 0
    event_queue_dropped: int = 0


class CalibrationController:
    def __init__(self) -> None:
        self.model = CalibrationModel()
        self.firmware_status = FirmwareStatus()
        self.impact_telemetry = ImpactTelemetry()
        self.bad_payload_count = 0

    def reset_session(self) -> None:
        self.model = CalibrationModel()
        self.firmware_status = FirmwareStatus()
        self.impact_telemetry = ImpactTelemetry()
        self.bad_payload_count = 0

    def handle_packet(self, packet: Packet) -> None:
        try:
            if packet.packet_type == CAL_PKT_METADATA:
                self.model.apply_metadata(packet.payload)
            elif packet.packet_type == CAL_PKT_SAMPLE_BATCH:
                self.model.append_samples(self._decode_samples(packet.payload))
            elif packet.packet_type == CAL_PKT_STATUS:
                self.firmware_status = FirmwareStatus(*struct.unpack("<IIII", packet.payload))
            elif packet.packet_type == CAL_PKT_IMPACT_EVENT:
                timestamp_us, outcome = struct.unpack("<IB", packet.payload)
                outcome_name = {
                    CAL_IMPACT_PROCESSED: "processed",
                    CAL_IMPACT_SKIPPED: "skipped",
                }.get(outcome)
                if outcome_name is None:
                    raise ValueError("invalid impact outcome")
                self.model.record_impact_event(
                    timestamp_us,
                    outcome_name,
                )
            elif packet.packet_type == CAL_PKT_IMPACT_TELEMETRY:
                self.impact_telemetry = ImpactTelemetry(*struct.unpack("<IIIIIII", packet.payload))
        except (IndexError, struct.error, ValueError):
            self.bad_payload_count += 1

    @staticmethod
    def _decode_samples(payload: bytes) -> list[Sample]:
        if not payload:
            raise ValueError("empty sample batch")
        count = payload[0]
        if not 1 <= count <= 16 or len(payload) != 1 + count * 14:
            raise ValueError("invalid sample batch")
        return [Sample(*struct.unpack_from("<IIhhh", payload, 1 + index * 14)) for index in range(count)]
