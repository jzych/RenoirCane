"""Read and validate a calibration UART stream without starting the GUI."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time

import serial

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "calibration_app"))

from app.controller import CalibrationController
from app.protocol import CAL_CMD_GET_METADATA, FrameParser, encode_frame
from app.serial_setup import reset_calibration_transport


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=float, default=10.0)
    args = parser.parse_args()

    frame_parser = FrameParser()
    controller = CalibrationController()
    with serial.Serial(args.port, 921600, timeout=0.1) as port:
        reset_calibration_transport(port)
        port.write(encode_frame(CAL_CMD_GET_METADATA, b""))
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            for packet in frame_parser.feed(port.read(4096)):
                controller.handle_packet(packet)

    config = controller.model.graph_config
    print(f"metadata={'yes' if config else 'no'} samples={len(controller.model.samples)} "
          f"firmware_dropped={controller.firmware_status.samples_dropped} "
          f"host_gaps={controller.model.host_sequence_gaps} "
          f"bad_frames={frame_parser.bad_packet_count} bad_payloads={controller.bad_payload_count}")
    return 0 if config is not None else 1


if __name__ == "__main__":
    raise SystemExit(main())
