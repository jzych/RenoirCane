from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib

from app.cobs import decode as cobs_decode
from app.cobs import encode as cobs_encode

PROTOCOL_VERSION = 2
CAL_PKT_METADATA = 0x01
CAL_PKT_SAMPLE_BATCH = 0x02
CAL_PKT_STATUS = 0x03
CAL_PKT_ERROR = 0x04
CAL_PKT_IMPACT_EVENT = 0x05
CAL_PKT_IMPACT_TELEMETRY = 0x06
CAL_PKT_LOG = 0x07
CAL_CMD_GET_METADATA = 0x80

CAL_IMPACT_PROCESSED = 0x01
CAL_IMPACT_SKIPPED = 0x02


@dataclass(frozen=True)
class Packet:
    version: int
    packet_type: int
    payload: bytes


def encode_frame(packet_type: int, payload: bytes) -> bytes:
    header = struct.pack("<BBH", PROTOCOL_VERSION, packet_type, len(payload))
    raw = header + payload
    return cobs_encode(raw + struct.pack("<I", zlib.crc32(raw) & 0xFFFFFFFF)) + b"\x00"


def decode_frame(frame: bytes) -> Packet:
    raw = cobs_decode(frame)
    if len(raw) < 8:
        raise ValueError("packet too short")
    version, packet_type, payload_length = struct.unpack_from("<BBH", raw)
    if version != PROTOCOL_VERSION or len(raw) != 8 + payload_length:
        raise ValueError("invalid packet header")
    expected_crc = struct.unpack_from("<I", raw, 4 + payload_length)[0]
    if zlib.crc32(raw[: 4 + payload_length]) & 0xFFFFFFFF != expected_crc:
        raise ValueError("invalid packet crc")
    return Packet(version, packet_type, raw[4 : 4 + payload_length])


class FrameParser:
    def __init__(self) -> None:
        self._buffer = bytearray()
        self.bad_packet_count = 0

    def feed(self, data: bytes) -> list[Packet]:
        self._buffer.extend(data)
        packets: list[Packet] = []
        while True:
            try:
                end = self._buffer.index(0)
            except ValueError:
                return packets
            frame = bytes(self._buffer[:end])
            del self._buffer[: end + 1]
            if not frame:
                continue
            try:
                packets.append(decode_frame(frame))
            except ValueError:
                self.bad_packet_count += 1
