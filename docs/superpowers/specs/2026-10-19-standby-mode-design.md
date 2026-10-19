# Low-Power Standby Mode Design

## Goal

Use the GPIO33 latching switch as a power-state selector: OFF sleeps and ON
runs the normal firmware. This corrects the original momentary-button
interpretation following hardware feedback and the user's explicit choice on
2026-10-20.

## Hardware and configuration

ON connects GPIO33 to GND (low); OFF opens it and the internal pull-up reads
high. `APP_FEATURE_SLEEP_SWITCH` defaults to `1` for the fitted-switch build.
On boards without a fitted switch, set `APP_FEATURE_SLEEP_SWITCH=0` (in
configuration or as a build flag). That build does not configure GPIO33 or
start the power task and remains awake. OFF and an absent switch are
electrically indistinguishable, so explicit configuration is required.

## Runtime behavior

The power task samples the GPIO33 level every 10 ms. ON/low cancels the
standby debounce and keeps normal operation running indefinitely. OFF/high
for 50 ms requests standby, including when OFF is already present at startup.
No interrupt, edge, extra ON toggle, or release is needed to enter sleep.

After accepting OFF, the power component:

1. Shows amber for 500 ms and then turns the LED off.
2. Calls the one-shot Bluetooth shutdown API, stopping PCM playback and new
   retries. An opening connection is allowed to finish before disconnecting.
   A 10-second bound covers the known JBL delay; timeout is logged and sleep
   still follows.
3. Disables accelerometer interrupts and writes LIS2DW12 CTRL1 ODR = 0.
4. Enables the GPIO33 RTC pull-up, disables its pull-down, retains RTC
   peripherals, and holds LED power GPIO13 high.
5. Enters deep sleep with EXT0 wake on GPIO33 low.

ON/low wakes through `app_main`, shows cyan for 200 ms, restores normal
startup, and stays awake while ON remains low. If the user returns ON during
shutdown, the level wake can happen immediately after deep-sleep entry; the
new boot stays awake rather than looping back to sleep.

## Component boundaries

- `power` owns level polling, OFF debounce, and standby sequencing.
  `power_is_entering_standby()` prevents normal sampling during shutdown.
- `bluetooth_audio_shutdown(uint32_t timeout_ms)` owns the one-shot radio
  lifecycle gate and bounded physical disconnect. Bluetooth policy files,
  speaker alternation and manual-reconnect behavior remain unchanged.
- `accelerometer_prepare_for_sleep()` owns sensor interrupt disabling and
  power-down register writes.
- `status_led` gives standby indication priority over late Bluetooth status,
  retains LED-off power during sleep, and releases its pad hold at startup.

## LED contract

| State | Indication |
| --- | --- |
| OFF accepted / entering standby | Amber for 500 ms |
| Standby | LED power disabled (off) |
| ON waking from EXT0 | Cyan for 200 ms, then normal Bluetooth status |

Normal power-on boot retains the existing boot indication. Existing connecting,
ready, playing and error colors remain unchanged.

## Reconnect cadence and calibration

`APP_BT_RECONNECT_MS = 2000U`. Retry selection, manual reconnect and media
policy are otherwise unchanged. Calibration mode does not initialize power
handling and ignores the switch.

## Failure handling

GPIO setup or task-creation failure is logged and shows the normal error LED.
Wake-source configuration is checked before normal services stop. A Bluetooth
shutdown timeout cannot prevent sleep indefinitely. A final RTC pin setup
failure logs reset-required and stops rather than entering sleep with an
invalid wake path.

## Verification

Native runtime tests cover held ON, OFF at startup without an edge, ON-to-OFF,
OFF bounce, ON during shutdown, and a disabled-switch build. The debounce
boundary is verified at 49,999 / 50,000 microseconds, and the Bluetooth retry
boundary at 1,999,999 / 2,000,000 microseconds. The complete Unity suite runs
on the host and the TinyPICO.

Hardware acceptance requires OFF -> amber/off/sleep and ON -> cyan/normal
operation, remaining awake while ON is held. A build with switch support
disabled must run for five minutes with GPIO33 unconnected. Record measured
idle-connected and standby battery currents and require at least a 5x ratio.
Speaker recovery and a subsequent real impact cue also remain required.

All work remains on the feature branch; merge requires explicit user consent.
