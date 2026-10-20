# Shared impact detection

Normal and calibration firmware consume the LIS2DW12 INT2 data-ready stream
at the configured 800 Hz. Both call `impact_process_sample()` in
`components/impact` with the acquired sample and `bluetooth_audio_start_impact`.
INT1 hardware taps never gate a cue.

The shared function uses the magnitude of the configured axis, including
`-32768` as magnitude `32768`. The inclusive software threshold remains
`APP_IMPACT_THRESHOLD_RAW = 4500` (2.196 g using the configured sensitivity).
One candidate is evaluated on a transition from below to at/above threshold;
the signal must fall below threshold to rearm, including after a skipped cue.
Changing sign while above threshold does not rearm detection.

The refractory interval remains `APP_IMPACT_REFRACTORY_MS = 200`. A crossing
within that interval is skipped without attempting playback. A crossing at
the exact end of the interval is eligible. As in the previous calibration
path, every eligible crossing starts refractory, including a playback attempt
that reports busy or disconnected. Refractory skips do not extend the interval.

For an eligible crossing, the shared function attempts actual Bluetooth
playback and reports processed, audio-busy, or audio-unavailable. Calibration
emits a green processed marker only for playback-started; every other candidate
emits a yellow skipped marker. Below-threshold samples and held crossings emit
no event. The graph's existing metadata threshold is the same software value
used in normal operation. Disconnect/error results are skipped, not audio-busy.

The sampling task owns sensor output reads in each mode. Normal diagnostic
logging reuses detection samples. Calibration tap diagnostics also reuse the
sample stream instead of reading a second sample that could consume data-ready.
GPIO handlers are installed before enabling INT2 routing; stale output is
drained before routing so startup does not wait forever on an already-high pin.

## Telemetry and compatibility

Protocol version 1 and its packed counters retain their meanings:

| Counter | Meaning |
| --- | --- |
| `int1_received` | Hardware tap interrupts received (diagnostic only). |
| `tap_confirmed` | Pending tap interrupt confirmed by the sensor status. |
| `threshold_rejected` | Confirmed hardware tap whose associated fresh sample is below the software threshold. This is a tap diagnostic, not a count of ordinary below-threshold stream samples. |
| `refractory_skipped` | Stream threshold crossings skipped during refractory. |
| `audio_busy_skipped` | Eligible stream crossings whose playback attempt reports busy. |
| `event_packets_sent` | Successfully transmitted processed/skipped event packets. |
| `event_queue_dropped` | Event packets dropped because the event queue is full. |

Tap interrupts are latched, so multiple interrupts pending before servicing can
coalesce for confirmation, as with the previous notification-based tap task.
A stream crossing can produce a cue without advancing either tap counter.
Normal mode disables INT1 routing because it has no tap telemetry consumer.

The hardware tap threshold is derived from the software threshold and rounded
down to the available FS/32 step, clamped to 1..31. At the current +/-16 g
range it is code 4, or 2 g; the exact 2.196 g threshold still belongs to the
software detector. The hardware tap axis follows the configured impact axis.
See ST's [LIS2DW12 application note, AN5038](https://www.st.com/resource/en/application_note/an5038-lis2dw12-alwayson-3d-accelerometer-stmicroelectronics.pdf)
for tap threshold quantization.

## Verification

Run `python3 tools/test_standby.py` on Linux/WSL for native sensor, power,
Bluetooth, and Unity regressions. Unity coverage exercises the shared function
at exact positive/negative threshold and signed saturation, held crossings,
refractory skips and the exact boundary, busy playback and rearming, and
disconnected/error results. The native sensor test checks interrupt setup
ordering and the derived diagnostic tap threshold. A native calibration-task
test injects an interrupt during sensor acquisition and verifies it waits for
the next sample, retries after read failure, and maintains markers/counters
without a second output reader. Build both firmware modes
and the `tinypico_test` Unity image with the pinned PlatformIO environment.

On 2026-10-20, both firmware modes and the Unity device image built successfully,
all 42 Unity tests
passed on the native runner, and the native sensor, calibration-task, standby,
and Bluetooth regressions passed. Independent review's logging-throughput and
tap/sample-ordering findings were corrected and re-reviewed with no remaining
actionable findings. Calibration app tests passed (63), as did audio tests (6).
No task-5 firmware has been uploaded during this work; the device-image build
used `--without-uploading --without-testing` and did not run tests on hardware.

Physical acceptance remains pending:

- Compare 20 deliberate impacts between normal and calibration builds, using
  the same speaker connection and threshold. Audible cues should correspond to
  green markers, with one cue per accepted crossing.
- Walk normally for one minute in each build and verify no cues or green
  markers.
- Confirm the app threshold line matches the level that triggers a cue in
  normal mode; verify skipped markers while audio is busy.

Host tests and firmware builds cannot establish these physical results.

## Malformed sensor frames (2026-10-20 correction)

A stationary-device capture reproduced three spontaneous accepted cues. The
triggering samples were `(19456, 7416, 514)`, `(192, 7244, 514)`, and
`(196, 5368, 514)`, while surrounding readings stayed near `(196, -1984, 528)`.
Each triggering frame had Z=514: its low byte violated the fixed-zero bits
1:0 in the LIS2DW12 output registers. See the output register tables in the
[ST datasheet, DS11811](https://www.st.com/resource/en/datasheet/lis2dw12.pdf).

`accelerometer_read_sample()` now rejects an entire frame if any axis violates
these fixed bits, leaving the caller's sample untouched. It does not mask the
corruption into a plausible value or change threshold/crossing/refractory rules.
Normal firmware reports discarded-frame totals and raw bytes at most once per
second. Calibration performs identical rejection without adding text to its
binary UART stream. The startup drain still requires a successful I2C transfer
but deliberately ignores payload validity, since that frame is discarded and
must not prevent later INT2 sampling.

Native driver regressions reproduce the three captured payloads and every
nonzero low-bit combination on all axes. They also check unchanged output on
rejection, valid rest/exact-threshold/saturation samples, and malformed-stale
startup followed by valid acquisition. Actual positive sensor saturation is
32764 in this left-aligned format; the shared classifier still accepts its
synthetic +32767 and -32768 test inputs as specified. Both firmware builds,
the native regressions, and all 42 Unity tests pass after this correction.

This validates a known-invalid input condition, not every possible corruption.
The underlying cause of the malformed bus payload remains unproven; BDU and
auto-increment were already enabled. Corruption that preserves the fixed bits
cannot be detected by this check alone. A stationary live check and deliberate
impact/cue confirmation are still required after uploading the correction.

### I2C interrupt isolation

The guard-only firmware discarded 27 malformed frames over 63.65 seconds with
the speaker connected and the cane still. A subsequent user check reported
three deliberate cues and one spontaneous cue; the latter sample
`(-140, -5668, -348)` passed the fixed-bit guard. Payload validation alone is
therefore insufficient to establish reliable operation.

The next correction installs the I2C driver from a short-lived task pinned
to CPU1, assigning its interrupt to that CPU. Both Bluetooth controller and
Bluedroid are configured on CPU0. The caller waits for installation completion
before using the sensor; semaphore/task-allocation and driver-install failures
are propagated with cleanup. Detection thresholds, timing, and sensor data rate
are unchanged in both modes. This follows a
[reported ESP-IDF workaround](https://github.com/espressif/esp-idf/issues/7781)
and requires a connected-speaker comparison to determine effectiveness here.

Native tests check the actual installing core and failure cleanup. Both
firmware builds and all 42 Unity tests pass with this change; physical
verification of the CPU1 change used normal firmware commit `99b9898`, uploaded
with verified flash hashes. Startup confirmed the I2C interrupt on CPU1.
The first 100-second capture connected to the speaker at 21326 ms and continued
through 99586 ms: 78.26 seconds connected, with zero malformed frames and zero
processed impact events. A further 75-second connected capture also recorded
zero malformed frames and zero processed events, with steady stationary
readings. This contrasts with the guard-only capture's 27 malformed frames in
63.65 connected seconds.

These observations support retaining CPU1 isolation for this board and build;
they do not prove the internal mechanism behind the ESP-IDF interference.
The final manual three-impact confirmation remains pending: no deliberate
impacts were captured in the last window. Full 20-impact/walking acceptance
also remains pending. Local evidence is `.pio/task5-cpu1-stationary.log` and
`.pio/task5-cpu1-impacts.log`.
