# GPIO33 standby mode

Normal firmware uses a latching switch between GPIO33 and GND. ON is closed
(low) and keeps normal operation running; OFF is open (high through the internal
pull-up) and requests sleep, including at startup. Calibration firmware ignores
the switch.

`APP_FEATURE_SLEEP_SWITCH` defaults to `1` for the fitted-switch build. For a
board without a switch, set it to `0` in configuration or add
`-DAPP_FEATURE_SLEEP_SWITCH=0` to build flags. That build leaves GPIO33 alone
and stays awake. An absent switch and an OFF switch both read high, so firmware
cannot auto-detect their difference.

OFF must remain high for 50 ms to enter standby. The LED shows amber for 500 ms,
then turns off. Playback and Bluetooth retries stop, an established A2DP link
disconnects, and the LIS2DW12 enters power-down (CTRL1 ODR = 0). An opening
A2DP connection is allowed to finish before disconnecting. The disconnect wait
is bounded at 10 seconds; timeout is logged and deep sleep still follows.

There is no extra press or release wait. GPIO33 uses EXT0 wake
on low, with the RTC pull-up enabled, pull-down disabled, and RTC peripherals
kept on. GPIO13 is held high to keep the LED power disabled throughout sleep.
Switching ON wakes the chip through `app_main`; the LED shows cyan for
200 ms and startup restores the sensor and speaker connection. Holding ON
keeps it awake; switching OFF sleeps again. The power task polls every 10 ms,
so OFF is recognized even without a GPIO-edge interrupt.

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
FreeRTOS calls. It checks OFF debounce, OFF at startup, held ON, disabled-switch builds, RTC
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
- [x] Observe OFF -> amber/off/sleep and ON -> cyan/normal operation; verify
  that holding ON stays awake (user confirmed; serial log captured two cycles).
- [ ] Separately power up with the switch already OFF and observe sleep without
  another toggle (covered by native runtime regression; physical check pending).
- [ ] Verify speaker reconnection and a full cue after a real impact.
- [ ] With `APP_FEATURE_SLEEP_SWITCH=0`, leave GPIO33 unconnected during
  5 minutes of normal use; verify no standby request in the serial log.

Record measured values and outcomes in `.tasks/database.md` after testing.
Task 4 stays in progress until these checks pass. Keep the feature branch for
review; merging requires the user's explicit consent.

## Latching-switch correction verification (2026-10-20)

Hardware feedback showed the original momentary-button mode alternately slept
and woke on ON while ignoring OFF. That mode is superseded by the level-based
behavior documented above; the user explicitly selected configured-switch mode.

The correction passed native held-ON, OFF-at-startup, ON-to-OFF, bounce,
ON-during-shutdown, and disabled-switch tests, plus all 35 Unity tests on the
host and on COM4. Both normal/calibration builds passed. The corrected normal
firmware was restored with verified flash hashes after device testing. A
read-only review of `4b92a88..ca7b344` found no actionable defects.

The corrected-firmware capture shows OFF accepted at uptime 42457 ms, sleep at
42957 ms, then a deep-sleep reset/wake. A second OFF was accepted 3537 ms after
wake and sleep logged at 6547 ms, followed by another wake. Samples continued
with ON held. The user confirmed amber/off for OFF and cyan/normal operation
for ON without sleeping again. Speaker/cue behavior, current readings,
physical power-up-already-OFF and disabled-switch unconnected observation
remain pending. Serial evidence is in local
`.pio/latching-switch-live.log` and `.pio/latching-device-check.log`.
