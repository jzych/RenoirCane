import struct
import zlib

from app.protocol import CAL_PKT_METADATA, FrameParser, Packet, encode_frame


def test_frame_round_trip_preserves_zero_payload_bytes():
    packet = Packet(version=1, packet_type=CAL_PKT_METADATA, payload=b"\x00\x11\x00")
    parser = FrameParser()

    assert parser.feed(encode_frame(packet.packet_type, packet.payload)) == [packet]


def test_parser_accepts_packet_split_across_reads():
    frame = encode_frame(CAL_PKT_METADATA, b"metadata")
    parser = FrameParser()

    packets = []
    for byte in frame:
        packets.extend(parser.feed(bytes([byte])))

    assert packets == [Packet(version=1, packet_type=CAL_PKT_METADATA, payload=b"metadata")]


def test_parser_recovers_after_corrupt_frame():
    valid = encode_frame(CAL_PKT_METADATA, b"ok")
    corrupt = bytearray(valid)
    corrupt[1] ^= 0x40
    parser = FrameParser()

    assert parser.feed(bytes(corrupt) + valid) == [Packet(version=1, packet_type=CAL_PKT_METADATA, payload=b"ok")]
    assert parser.bad_packet_count == 1


def test_packet_payload_length_is_validated():
    header = struct.pack("<BBH", 1, CAL_PKT_METADATA, 4)
    raw = header + b"bad" + struct.pack("<I", zlib.crc32(header + b"bad") & 0xFFFFFFFF)
    from app.cobs import encode

    parser = FrameParser()
    assert parser.feed(encode(raw) + b"\x00") == []
    assert parser.bad_packet_count == 1
