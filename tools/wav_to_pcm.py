#!/usr/bin/env python3
"""Convert the approved impact WAV into one second of A2DP-ready PCM."""

from __future__ import annotations

import argparse
import struct
import wave
from pathlib import Path


INPUT_RATE_HZ = 48_000
OUTPUT_RATE_HZ = 44_100
CHANNELS = 2
INPUT_WIDTH_BYTES = 3
OUTPUT_WIDTH_BYTES = 2
OUTPUT_FRAMES = OUTPUT_RATE_HZ


def read_s24le(data: bytes, offset: int) -> int:
    value = int.from_bytes(data[offset : offset + 3], "little", signed=False)
    return value - (1 << 24) if value & (1 << 23) else value


def clamp_s16(value: int) -> int:
    return max(-32768, min(32767, value))


def convert(source: Path, destination: Path) -> int:
    with wave.open(str(source), "rb") as wav:
        if (
            wav.getnchannels() != CHANNELS
            or wav.getsampwidth() != INPUT_WIDTH_BYTES
            or wav.getframerate() != INPUT_RATE_HZ
            or wav.getcomptype() != "NONE"
        ):
            raise ValueError("expected PCM 48 kHz, 24-bit, stereo WAV input")
        if wav.getnframes() < INPUT_RATE_HZ:
            raise ValueError("input must contain at least one second of audio")
        source_bytes = wav.readframes(INPUT_RATE_HZ)

    frame_width = CHANNELS * INPUT_WIDTH_BYTES
    output = bytearray(OUTPUT_FRAMES * CHANNELS * OUTPUT_WIDTH_BYTES)
    for out_frame in range(OUTPUT_FRAMES):
        source_position = out_frame * INPUT_RATE_HZ / OUTPUT_RATE_HZ
        low_frame = int(source_position)
        high_frame = min(low_frame + 1, INPUT_RATE_HZ - 1)
        fraction = source_position - low_frame
        for channel in range(CHANNELS):
            low = read_s24le(source_bytes, low_frame * frame_width + channel * 3)
            high = read_s24le(source_bytes, high_frame * frame_width + channel * 3)
            interpolated = round(low + (high - low) * fraction)
            sample = clamp_s16(interpolated >> 8)
            struct.pack_into("<h", output, (out_frame * CHANNELS + channel) * 2, sample)

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(output)
    return len(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    size = convert(args.source, args.destination)
    print(f"wrote {OUTPUT_FRAMES} frames ({size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
