# Status LED

Normal and calibration firmware share `status_led_on_bluetooth_state()`.
The Bluetooth policy and its events are unchanged. The APA102 uses data GPIO2,
clock GPIO12, and active-low power GPIO13.

| Indication | Meaning | RGB channel values |
| --- | --- | --- |
| Off | LED disabled; also task-4 deep sleep | Power GPIO13 high |
| Steady dim white | Boot | 8, 8, 8 |
| Steady blue | Searching, connecting, or disconnected between retries | 0, 0, 16 |
| Steady green | Speaker connected and idle, including streaming silence | 0, 16, 0 |
| Cyan for the cue | `audio_is_active()` while connected | 0, 12, 12 |
| Steady red | Error | 16, 0, 0 |
| Amber for 500 ms | Entering standby, owned by task 4 | 16, 8, 0 |
| Cyan for 200 ms | Wake from deep sleep, owned by task 4 | 0, 16, 16 |

The APA102 global brightness byte remains `0xE2` for every lit state.
Task 7 retains the existing standby enum values, colors, and durations.

`BT_AUDIO_DISABLED` and `BT_AUDIO_CONNECTING` both select blue immediately.
`BT_AUDIO_CONNECTED` and `BT_AUDIO_STREAMING` select the connected indication;
the continuous A2DP media stream does not itself select cyan. A small FreeRTOS
task samples `audio_is_active()` every 10 ms and switches only between green
and cyan. It sends a new LED frame only when that playback indication changes.
The one-second cue therefore produces approximately one second of cyan, with
up to a polling interval plus scheduler delay at each edge. This tracks PCM
consumption, so Bluetooth/speaker buffering can shift the audible cue relative
to the LED.

The existing spinlock now protects the tracked LED state as well as the full
APA102 frame and power-off writes. The playback task releases the audio lock
before taking the LED lock. It rechecks the selected state under the LED lock,
so disconnected, error, boot, off, and standby indications cannot be replaced
by a playback update. Once entering standby, later Bluetooth callbacks cannot
replace amber or turn the LED back on after power-off. Failure to allocate the
playback task holds normal status requests at red; off and task-4 power
indications remain available.

## Automated verification

Run `python3 tools/test_standby.py` under Linux/WSL. The status LED suite runs
the production driver against GPIO capture, with real PCM playback and native
mutexes replacing the ESP32 critical-section primitive. It checks:

- Connected idle and streaming silence remain green.
- Cue start becomes cyan; consuming the complete embedded PCM returns green.
- Disconnect changes directly to blue, even with an active cue.
- Error, invalid state input, worker allocation failure, and standby priority.
- Two concurrent threads call `status_led_set()` 500 times each. Every captured
  96-bit APA102 frame has valid start/end bytes, brightness, and a defined color.

The calibration startup regression also verifies registration of the shared
callback. The existing power suite checks task-4 amber/wake timing and LED
power retention. Build both `tinypico` and `tinypico_calibration`; compile the
`tinypico_test` device image separately. Native tests do not establish physical
speaker-off timing or real FreeRTOS scheduling.

Validation on 2026-10-20: the complete native suite passed, including all 43
Unity cases and the 1000-call LED test. Both normal and calibration firmware
builds passed, as did all 70 app tests and 6 audio tests (plus 3 subtests).
The Unity device image compiled with upload and device execution disabled.
Independent review found no code issues. The first combined Python test
invocation failed collection because it ran the app suite from the repository
root; rerunning from `calibration_app` resolved those import errors.

## Pending hardware acceptance

- With the speaker connected and idle, confirm steady green in both modes.
- Trigger accepted impacts and confirm cyan for about the cue duration, then
  green. Confirm rejected/busy impacts do not extend the indication.
- Switch off the speaker while idle and during a cue. Measure from the physical
  power-off to blue; the task target is at most 200 ms. The driver changes blue
  synchronously when Bluetooth reports disconnect, but Bluetooth link-loss
  detection may itself take longer. Do not treat callback latency as a measured
  speaker-off result.
- Confirm reconnect returns green and normal-mode standby still shows amber,
  off, then cyan on wake. Calibration retains its existing lack of standby.
- Confirm the two-task stress scenario on the actual device if hardware-level
  stress acceptance is required; the automated 1000-call result is native.

No task-7 firmware has been uploaded or physically verified yet. The feature
branch must remain separate from main until the user explicitly consents to
merging.
