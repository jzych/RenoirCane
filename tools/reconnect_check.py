"""Hardware check: `bt reconnect` while an A2DP link to a powered speaker is opening.

Resets the device, waits for the trigger log line, sends `bt reconnect`, and measures the time from the
disconnect of the established link to the next "connected to configured speaker".

Limits include the ~5 s Bluedroid delay-report wait that every connection to the JBL speakers incurs.
"""

import argparse
import re
import time

import serial

KEYS = ("connected to configured", "disconnecting established", "BT_AUDIO", "bt_reconnect",
        "BTA_AV_OPEN_EVT", "hcif conn complete", "connect request")
# primary: page (<=2.8 s) + delay-report wait (5 s) + open
# secondary: primary page timeout (~5.1 s) + retry cadence (5 s) + page + delay-report wait + open
LIMIT_MS = {"primary": 10000, "secondary": 20000}
LOG_LINE = re.compile(r"^[IWE] \((\d+)\) (.*)$")


def capture(port_name, duration_s, delay_s, trigger):
    port = serial.Serial(port_name, 115200, timeout=0.05)
    port.dtr = False
    port.rts = True
    time.sleep(0.1)
    port.rts = False
    start = time.time()
    buf = b""
    triggered_at = None
    sent = False
    while time.time() - start < duration_s:
        buf += port.read(4096)
        if triggered_at is None and trigger in buf.decode("utf-8", "replace"):
            triggered_at = time.time()
        if triggered_at and not sent and time.time() - triggered_at >= delay_s:
            port.write(b"bt reconnect\r\n")
            sent = True
            print(f"[host] sent 'bt reconnect' {time.time() - triggered_at:.2f}s after trigger")
    port.close()
    text = re.sub(r"\x1b\[[0-9;]*m", "", buf.decode("utf-8", "replace"))
    return [line.strip() for line in text.splitlines() if any(key in line for key in KEYS)]


def evaluate(lines):
    events = [(int(m.group(1)), m.group(2)) for m in map(LOG_LINE.match, lines) if m]
    requested_at = next((t for t, msg in events if "disconnecting established" in msg), None)
    if requested_at is None:
        return False, "reconnect was not deferred to an established link"
    disconnected_at = next(
        (t for t, msg in events if t >= requested_at and "configured speaker disconnected" in msg), None)
    if disconnected_at is None:
        return False, "established link was not disconnected"
    speaker = None
    for t, msg in events:
        if t <= disconnected_at:
            continue
        if "connect request to" in msg:
            speaker = msg.split("connect request to ")[1].split()[0]
        if "connected to configured speaker" in msg:
            elapsed = t - disconnected_at
            passed = speaker is not None and elapsed <= LIMIT_MS[speaker]
            return passed, f"{speaker} reconnected {elapsed} ms after disconnect (limit {LIMIT_MS.get(speaker)} ms)"
    return False, "no reconnect within the capture window"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=float, default=50.0)
    parser.add_argument("--delay", type=float, default=0.05, help="seconds between trigger and `bt reconnect`")
    parser.add_argument("--trigger", default="connect request to secondary")
    args = parser.parse_args()

    lines = capture(args.port, args.seconds, args.delay, args.trigger)
    print("\n".join(lines))
    passed, detail = evaluate(lines)
    print(f"{'PASS' if passed else 'FAIL'}: {detail}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
