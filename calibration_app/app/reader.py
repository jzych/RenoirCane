import threading

from app.controller import CalibrationController
from app.diagnostics import diagnose
from app.protocol import FrameParser
from app.serial_backends.base import SerialBackend


class CalibrationReader:
    def __init__(self, backend: SerialBackend, controller: CalibrationController) -> None:
        self._backend = backend
        self._controller = controller
        self._parser = FrameParser()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._error: str | None = None

    def start(self) -> None:
        self._stop.clear()
        self._error = None
        self._thread = threading.Thread(target=self._run, name="calibration-reader", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=1.0)

    @property
    def error(self) -> str | None:
        return self._error

    def _run(self) -> None:
        try:
            while not self._stop.is_set() and self._backend.connected:
                previous_bad_packets = self._parser.bad_packet_count
                packets = self._parser.feed(self._backend.read(4096))
                if packets:
                    diagnose(f"Reader decoded packets={len(packets)}")
                if self._parser.bad_packet_count != previous_bad_packets:
                    diagnose(f"Reader bad_packets={self._parser.bad_packet_count}")
                for packet in packets:
                    self._controller.handle_packet(packet)
        except Exception as error:
            self._error = str(error)
            self._backend.close()
