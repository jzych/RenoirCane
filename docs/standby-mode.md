# GPIO33 standby mode

Normal firmware supports an optional momentary button between GPIO33 and GND.
Leave GPIO33 unconnected when no button is fitted; the firmware enables its
internal pull-up. Calibration firmware does not initialize the standby task.

Press for at least 50 ms to enter standby. The LED shows amber for 500 ms,
then turns off. Playback and Bluetooth retries stop, an established A2DP link
disconnects, and the LIS2DW12 enters power-down (CTRL1 ODR = 0). An opening
A2DP connection is allowed to finish before disconnecting. The disconnect wait
is bounded at 10 seconds; timeout is logged and deep sleep still follows.

Release the button to let the device enter deep sleep. GPIO33 uses EXT0 wake
on low, with the RTC pull-up enabled, pull-down disabled, and RTC peripherals
kept on. GPIO13 is held high to keep the LED power disabled throughout sleep.
The next press wakes the chip through `app_main`; the LED shows cyan for
200 ms and startup restores the sensor and speaker connection. The wake press
must be released stably before another standby press can be accepted.

Normal Bluetooth retries now use `APP_BT_RECONNECT_MS = 2000U`. The primary /
secondary selection and manual-reconnect policy retain their existing rules.

## Automated checks

Build `tinypico`, `tinypico_calibration`, and the `tinypico_test` Unity image
with the pinned PlatformIO environment. If CMake has cached an older component
source list, clean the affected environment before rebuilding.

With a Linux host compiler (or Ubuntu under WSL), run:

```sh
python3 tools/test_standby.py
```

Build the Unity image once first so `.pio/libdeps/tinypico_test/Unity` exists.
The native runner exercises the real power task, LED driver, Bluetooth
adapter, sensor driver, and existing Unity suite, replacing only hardware and
FreeRTOS calls. It checks debounce, an absent button, a held wake press, RTC
wake/pull/LED-hold setup, disconnect ordering and timeout, and the exact
two-second retry boundary. Host simulation does not measure current or prove
physical speaker/LED behavior.

The audio conversion suite uses the standard library and can run without
pytest:

```sh
python -m unittest discover -s test/test_audio_asset -v
```

## Physical acceptance checklist

TinyPICO was identified on COM4 (CH9102, ESP32-PICO-D4, MAC
`0c:8b:95:aa:85:f8`) on 2026-10-19. All 35 Unity tests passed on the device;
normal `tinypico` standby firmware was then restored and its flash hash verified.
Startup detected the LIS2DW12 at 0x19 and continuously reported accelerometer
samples during a 25-second serial observation, without a reset loop or standby
entry. Both configured speaker attempts timed out; the next attempts started
2050 ms after the disconnected events, consistent with the configured 2000 ms
interval and 50 ms worker polling. Speaker connection and cue playback are
still unverified. No firmware was uploaded to the STM32 devices on COM6/COM8.

Use the same battery supply with USB unplugged for both current measurements;
record serial observations separately so charging cannot skew the comparison.

- [x] Run the Unity suite on the TinyPICO, then restore `tinypico` firmware
  (35 passed, 0 failed).
- [ ] Measure idle-connected battery current: pending.
- [ ] Measure deep-sleep battery current: pending; require at least a 5x
  reduction (`idle current / standby current >= 5`).
- [ ] Observe amber entry, LED-off sleep, and cyan wake, including a held first
  press and a held wake press; verify no wake/sleep loop.
- [ ] Verify speaker reconnection and a full cue after a real impact.
- [ ] Leave GPIO33 unconnected during 5 minutes of normal use; verify no
  standby request in the serial log.

Record measured values and outcomes in `.tasks/database.md` after testing.
Task 4 stays in progress until these checks pass. Keep the feature branch for
review; merging requires the user's explicit consent.
