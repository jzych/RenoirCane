from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CONVERTER = REPO_ROOT / "tools" / "wav_to_pcm.py"


class WavToPcmTests(unittest.TestCase):
    def test_converts_first_second_of_48khz_24bit_stereo_wav(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            source = temp / "source.wav"
            output = temp / "output.pcm"
            frames = (b"\x00\x00\x00\x00\x00\x00") * 96_000

            with wave.open(str(source), "wb") as wav:
                wav.setnchannels(2)
                wav.setsampwidth(3)
                wav.setframerate(48_000)
                wav.writeframes(frames)

            completed = subprocess.run(
                [sys.executable, str(CONVERTER), str(source), str(output)],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(output.stat().st_size, 44_100 * 2 * 2)
            self.assertEqual(output.read_bytes(), b"\x00" * (44_100 * 2 * 2))


if __name__ == "__main__":
    unittest.main()
