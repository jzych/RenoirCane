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


def raw_frame(raw: bytes) -> bytes:
    from app.cobs import encode

    return encode(raw) + b"\x00"


def test_parser_rejects_a_frame_whose_crc_does_not_match():
    header = struct.pack("<BBH", 1, CAL_PKT_METADATA, 2)
    good_crc = zlib.crc32(header + b"ok") & 0xFFFFFFFF
    parser = FrameParser()

    # Header and length are valid; only the CRC is wrong.
    assert parser.feed(raw_frame(header + b"ok" + struct.pack("<I", good_crc ^ 1))) == []
    assert parser.bad_packet_count == 1


def test_parser_rejects_frames_shorter_than_header_and_crc():
    parser = FrameParser()

    assert parser.feed(raw_frame(b"\x01\x01\x00") + raw_frame(b"\x01\x01\x00\x00\x00\x00\x00")) == []
    assert parser.bad_packet_count == 2


def test_parser_rejects_an_unknown_protocol_version():
    header = struct.pack("<BBH", 2, CAL_PKT_METADATA, 0)
    parser = FrameParser()

    assert parser.feed(raw_frame(header + struct.pack("<I", zlib.crc32(header) & 0xFFFFFFFF))) == []
    assert parser.bad_packet_count == 1


def test_parser_counts_invalid_cobs_and_keeps_parsing():
    parser = FrameParser()
    valid = encode_frame(CAL_PKT_METADATA, b"ok")

    # Code byte 0x05 claims four data bytes but only one follows.
    assert parser.feed(b"\x05\x01\x00" + valid) == [Packet(1, CAL_PKT_METADATA, b"ok")]
    assert parser.bad_packet_count == 1


def test_frame_round_trip_handles_runs_longer_than_one_cobs_block():
    from app.cobs import decode, encode

    payload = bytes(range(1, 256)) * 2  # 510 non-zero bytes: needs 0xFF block splitting
    encoded = encode(payload)

    assert b"\x00" not in encoded
    assert decode(encoded) == payload
    assert FrameParser().feed(encode_frame(CAL_PKT_METADATA, payload)) == [
        Packet(1, CAL_PKT_METADATA, payload)
    ]
