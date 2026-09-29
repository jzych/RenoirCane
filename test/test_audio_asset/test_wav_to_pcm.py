from __future__ import annotations

import importlib.util
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CONVERTER = REPO_ROOT / "tools" / "wav_to_pcm.py"

_spec = importlib.util.spec_from_file_location("wav_to_pcm", CONVERTER)
wav_to_pcm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(wav_to_pcm)

OUTPUT_BYTES = 44_100 * 2 * 2
# 48 kHz and 44.1 kHz share a sample instant every 480 input / 441 output frames.
ALIGNED_IN, ALIGNED_OUT = 480, 441


def s24(value: int) -> bytes:
    return (value & 0xFFFFFF).to_bytes(3, "little")


def write_wav(path: Path, frames: bytes, channels=2, width=3, rate=48_000) -> None:
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(width)
        wav.setframerate(rate)
        wav.writeframes(frames)


def ramp_frames() -> bytes:
    """First second: left = +n*128, right = -n*128. Second second: full-scale, must be ignored."""
    first = b"".join(s24(n * 128) + s24(-n * 128) for n in range(48_000))
    second = (s24(0x7FFF00) + s24(0x7FFF00)) * 48_000
    return first + second


def output_frame(pcm: bytes, index: int) -> tuple[int, int]:
    return struct.unpack_from("<hh", pcm, index * 4)


class WavToPcmTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.temp = Path(self._temp.name)
        self.source = self.temp / "source.wav"
        self.output = self.temp / "output.pcm"

    def tearDown(self) -> None:
        self._temp.cleanup()

    def convert(self) -> bytes:
        self.assertEqual(wav_to_pcm.convert(self.source, self.output), OUTPUT_BYTES)
        return self.output.read_bytes()

    def test_converts_first_second_of_48khz_24bit_stereo_wav(self) -> None:
        write_wav(self.source, ramp_frames())

        completed = subprocess.run(
            [sys.executable, str(CONVERTER), str(self.source), str(self.output)],
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("wrote 44100 frames", completed.stdout)
        pcm = self.output.read_bytes()
        self.assertEqual(len(pcm), OUTPUT_BYTES)
        self.assertEqual(output_frame(pcm, 0), (0, 0))
        # Last output frame interpolates inside the first second, never the full-scale second one.
        self.assertEqual(output_frame(pcm, 44_099), (23_999, -24_000))

    def test_aligned_frames_keep_24bit_value_scaled_to_16bit_per_channel(self) -> None:
        write_wav(self.source, ramp_frames())

        pcm = self.convert()

        for m in range(100):
            expected = m * ALIGNED_IN * 128 >> 8
            self.assertEqual(output_frame(pcm, m * ALIGNED_OUT), (expected, -expected), m)

    def test_resamples_by_linear_interpolation(self) -> None:
        # Alternating full-scale/zero: output frame 1 lies at source 1.088, between 0x7FFF00 and 0.
        frames = b"".join(
            s24(0x7FFF00 if n % 2 else 0) * 2 for n in range(48_000)
        )
        write_wav(self.source, frames)

        pcm = self.convert()

        self.assertEqual(output_frame(pcm, 1), (29_869, 29_869))

    def test_silence_stays_silent(self) -> None:
        write_wav(self.source, b"\x00" * 6 * 96_000)

        self.assertEqual(self.convert(), b"\x00" * OUTPUT_BYTES)

    def test_rejects_non_matching_formats(self) -> None:
        cases = {
            "mono": dict(channels=1, width=3, rate=48_000),
            "16-bit": dict(channels=2, width=2, rate=48_000),
            "44.1 kHz": dict(channels=2, width=3, rate=44_100),
        }
        for name, fmt in cases.items():
            with self.subTest(name):
                frame_bytes = fmt["channels"] * fmt["width"]
                write_wav(self.source, b"\x00" * frame_bytes * fmt["rate"], **fmt)
                with self.assertRaisesRegex(ValueError, "48 kHz, 24-bit, stereo"):
                    wav_to_pcm.convert(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_input_shorter_than_one_second(self) -> None:
        write_wav(self.source, b"\x00" * 6 * 47_999)

        with self.assertRaisesRegex(ValueError, "at least one second"):
            wav_to_pcm.convert(self.source, self.output)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
