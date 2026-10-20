# Shared impact detection

Normal and calibration firmware use the original normal-mode tap-gated path.
Both call `impact_read_sample()` in `components/impact`: for a pending INT1
interrupt it confirms the sensor tap status, clears the latch, then reads the
sample in that order. Both pass the acquired sample and confirmation flag to
`impact_process_sample()` with `bluetooth_audio_start_impact`.

The shared function uses the magnitude of the configured axis, including
`-32768` as magnitude `32768`. The inclusive software threshold is
`APP_IMPACT_THRESHOLD_RAW = 4500` (2.196 g using the configured sensitivity),
restored from the working normal firmware. An unconfirmed sample cannot
request playback, even at saturation. A distinct confirmed tap does not need
an intervening below-threshold sample; the hardware tap engine supplies the
event gate. The threshold-crossing classifier remains available for isolated
algorithm tests but does not decide production cues.

The refractory interval remains `APP_IMPACT_REFRACTORY_MS = 200`. A tap candidate
within that interval is skipped without attempting playback. A candidate at
the exact end of the interval is eligible. As in the original normal
path, every eligible candidate starts refractory, including a playback attempt
that reports busy or disconnected. Refractory skips do not extend the interval.

For an eligible tap, the shared function attempts actual Bluetooth
playback and reports processed, audio-busy, or audio-unavailable. Calibration
emits a green processed marker only for playback-started; every other candidate
emits a yellow skipped marker. Unconfirmed and below-threshold samples emit
no event. The graph's metadata threshold is the same post-tap software value
used in normal operation; a graph peak alone is insufficient to trigger a cue.
Disconnect/error results are skipped, not audio-busy.

One task owns sensor output reads in each mode. Normal mode wakes on INT1,
with a one-second timeout for diagnostics, and retains its original task
priority 6. Calibration keeps the INT2 graph stream at the configured 800 Hz;
INT1 also wakes its sampling task immediately for post-tap acquisition. A tap
arriving during a read belongs to the next read. A failed post-tap acquisition
consumes the cleared tap, matching original normal behavior; it cannot arm an
unrelated later stream sample. GPIO handlers are installed before interrupt
routing. Stale INT1 latches are cleared, and stale INT2 output is drained.

## Telemetry and compatibility

Firmware and app now use protocol version 2. Packet layouts remain unchanged;
the version distinguishes tap-candidate skip counters from the previous
stream-crossing skip counters. A version-1 calibration app must be updated
alongside the firmware. Metadata sample-format version remains 1.

| Counter | Meaning |
| --- | --- |
| `int1_received` | Hardware tap interrupts received. |
| `tap_confirmed` | Pending tap interrupt confirmed by the sensor status. |
| `threshold_rejected` | Confirmed tap whose valid post-tap sample is below the software threshold. Ordinary stream lows do not count. |
| `refractory_skipped` | Above-threshold, confirmed tap candidates skipped during refractory. |
| `audio_busy_skipped` | Eligible tap candidates whose playback attempt reports busy. |
| `event_packets_sent` | Successfully transmitted processed/skipped event packets. |
| `event_queue_dropped` | Event packets dropped because the event queue is full. |

Tap interrupts are latched, so multiple interrupts pending before servicing can
coalesce for confirmation, as with the previous notification-based tap task.
A cue always requires a confirmed tap; both modes enable INT1 routing.

The original hardware tap profile is restored exactly: `TAP_THS_X=0x00`,
`TAP_THS_Y=0x08`, `TAP_THS_Z=0x40`, and `INT_DUR=0x06`, with Y tap detection
enabled. At +/-16 g, the hardware gate is 4 g. This is intentionally separate
from the original 2.196 g software check on the later post-tap sample. Deriving
the hardware gate from 4500 raw would lower it to 2 g and change the working
normal-mode behavior again.
See ST's [LIS2DW12 application note, AN5038](https://www.st.com/resource/en/application_note/an5038-lis2dw12-alwayson-3d-accelerometer-stmicroelectronics.pdf)
for tap threshold quantization.

## Verification

Run `python3 tools/test_standby.py` on Linux/WSL for native sensor, power,
Bluetooth, and Unity regressions. Unity coverage exercises the shared function
at exact positive/negative threshold and signed saturation, missing tap
confirmation, distinct taps without software rearming, refractory boundaries,
busy playback, and disconnected/error results. The native sensor test checks
interrupt setup ordering and the original tap registers. The native
calibration-task test checks status/clear/read ordering, immediate INT1 wake,
an interrupt arriving during acquisition, failed-read tap consumption, and
markers/counters with one output reader. Both decoders reject legacy protocol
frames. Build both firmware modes
and the `tinypico_test` Unity image with the pinned PlatformIO environment.

On 2026-10-20, both firmware modes and the Unity device image built successfully,
all 42 Unity tests
passed on the native runner, and the native sensor, calibration-task, standby,
and Bluetooth regressions passed. Independent review's logging-throughput and
tap/sample-ordering findings were corrected and re-reviewed with no remaining
actionable findings. Calibration app tests passed (63), as did audio tests (6).
At that initial verification, no task-5 firmware had been uploaded; the
device-image build used `--without-uploading --without-testing` and did not run
tests on hardware. Later uploads and observations are recorded below.

Physical acceptance remains pending:

- Compare 20 deliberate impacts between normal and calibration builds, using
  the same speaker connection and threshold. Audible cues should correspond to
  green markers, with one cue per accepted tap.
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
The user subsequently reported three deliberate cues and three additional
cues, clarifying that the extras followed separate light contacts or handling.
No deliberate impacts were captured in the last serial window. Full
20-impact/walking acceptance remains pending. Local evidence is
`.pio/task5-cpu1-stationary.log` and `.pio/task5-cpu1-impacts.log`.

### Initial user-approved sensitivity trial (4 g)

Following that clarification, the user approved raising the shared threshold
from 4500 raw (2.196 g) to 8200 raw (4.0016 g). This explicitly authorizes
retuning beyond the original task-5 scope. Normal and calibration modes use
the same new threshold, including calibration metadata; refractory remains
200 ms. A regression exercises the configured processor with the previously
captured unwanted sample (-5668), a strong deliberate impact (-32732), and
the new inclusive positive/negative threshold boundaries. The unwanted sample
must not request playback, while the strong impact and exact boundaries do.

This was a physical sensitivity trial: the recorded unwanted sample predates
CPU1 isolation and does not establish the amplitude of the later handling
contacts. The subsequent user check reported three normal and three light tip
contacts all producing cues, so this setting did not meet sensitivity needs.

With the trial threshold, all 42 Unity tests and the native sensor,
calibration-task, power, and Bluetooth regressions passed. Both `tinypico`
and `tinypico_calibration` built successfully; review found no actionable
issues. The configured-processor regression was first observed failing with
the old threshold, then passing after the change.

### Waveform-based sensitivity trial (10 g)

A full calibration-stream capture collected 62955 samples. The user labelled
the seven contacts as one sideways hit, three normal hits, and three light tip
contacts. Full Y-axis peaks (rather than the first crossing samples logged by
normal firmware) were:

| Contact type | Peak raw magnitudes | Peak acceleration |
| --- | --- | --- |
| Sideways | 9964 | 4.86 g |
| Normal | 29352, 26264, 32732 | 14.32, 12.82, 15.97 g |
| Light | 10708, 18424, 9828 | 5.23, 8.99, 4.80 g |

The labelled window had no sample-sequence gaps or invalid protocol frames.
Startup decoding errors and six sequence gaps were confined to the earlier
startup period. Firmware sample-drop counters remained zero. Local evidence
is `.pio/task5-contact-waveforms-retry.csv` and its accompanying raw capture
and packet log.

Offline replay through the actual shared C detector at 20500 raw (10.004 g)
accepted exactly the three normal hits and rejected the sideways and three
light contacts. The user approved applying this threshold to both modes,
including calibration metadata, with refractory unchanged at 200 ms. The
configured-processor regression now rejects the captured sideways/light
peaks and accepts all three normal peaks plus exact positive/negative 20500
boundaries. The diagnostic tap threshold follows the new setting as code 20.

This small labelled capture supports a further sensitivity trial, not a
general guarantee across users, surfaces, or walking conditions. Live response
is recorded below; full task-5 physical acceptance remains pending.

With 20500 configured, all 42 Unity tests and the native sensor,
calibration-task, power, and Bluetooth regressions passed. Both firmware modes
built successfully, and review found no actionable issues. The updated contact
regression was first observed failing at the old 8200 setting.

#### Live outcome and remaining sensitivity issue

Normal firmware `063ad99` was uploaded with verified flash hashes. Startup
confirmed threshold 20500, I2C interrupt on CPU1, and speaker connection.
The user reported all three normal hits cued, with the middle of three light
contacts also cuing. The 60-second log had exactly four accepted events and no
malformed frames; the unwanted contact's first crossing magnitude was 24072
(at least 11.75 g, not its full measured peak). This trial improved rejection
but did not resolve light-contact sensitivity.

A second full calibration capture confirmed metadata at 800 Hz, +/-16 g,
Y axis, threshold 20500. Its three normal peaks were 32736, 30808, and 32736;
the three light peaks were 25928, 25684, and 21860 (12.65, 12.53, and 10.67 g).
All six contacts had playback-started firmware markers; the user confirmed a
light contact also sounded. The labelled window had no sequence gaps or invalid
protocol frames. These light peaks approach the first capture's weakest normal
peak of 26264 (12.82 g), leaving little margin for threshold-only tuning.
Normal `063ad99` firmware was restored afterward and its startup and speaker
connection verified. Evidence is `.pio/task5-10g-trial.log`,
`.pio/task5-10g-contact-waveforms.csv`, and
`.pio/task5-10g-normal-restored-startup.log`.

An offline hypothesis uses the first 20500 crossing to start a 100 ms window,
freezes the mean of the preceding 64 Y samples, and accumulates squared
deviation from that baseline over actual sample intervals. With zero-order
hold and a level of 2e12 raw-squared microseconds, all six normal contacts in
the two captures reached confirmation in 37-84 ms. All six light contacts and
the sideways hit were rejected by the amplitude gate or strength requirement.
This is a small-sample diagnostic calculation, not an implemented filter.
Adding confirmation would introduce up to 100 ms of decision latency and
requires a new design decision; no change beyond the approved 10 g threshold
has been made to production firmware.

### Restoring the working normal detector

The user clarified that normal firmware before task 5 worked without unwanted
triggers, and approved preserving that path in both modes. Comparison with
`f1c8064` showed that task 5 had removed its hardware tap gate. In the first
labelled capture, hardware tap counters advanced only around the three normal
hits, not the sideways hit or three light contacts. The correction above
restores that gate, exact hardware profile, original software threshold, and
post-tap read order. CPU1 I2C isolation and malformed-frame rejection remain.
The proposed burst-strength filter was not implemented.

A native calibration-task regression first reproduced an ungated saturation
sample incorrectly requesting playback, then passed with tap gating. All 43
native Unity tests, sensor/sampling-task/power/Bluetooth regressions, and 64
calibration-app tests pass. Both firmware modes and the Unity device image
built successfully. The device image was built without uploading or running
device tests. Review's protocol-version finding was corrected and re-reviewed
with no remaining actionable issues. Live verification of the restored path
and the full normal/calibration physical comparison remain pending.
