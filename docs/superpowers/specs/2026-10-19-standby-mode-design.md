# Low-Power Standby Mode Design

## Goal

Put the normal firmware into low-power standby after a deliberate GPIO33
button press, and wake it with a second press without changing calibration
mode, impact detection, or Bluetooth speaker-selection policy.

## Hardware Contract

GPIO33 is an optional, active-low momentary switch: the switch connects the
pin to GND when pressed. Firmware enables GPIO33's internal pull-up, so an
unfitted switch reads high rather than floating. GPIO33 is an RTC-capable pin
and is the EXT0 deep-sleep wake source.

## Sleep Model

The firmware uses deep sleep. It gives the required low standby current and
has an unambiguous wake path: wake resets the ESP32 and execution restarts at
`app_main`. Normal startup then initializes the accelerometer and Bluetooth;
the existing Bluetooth policy performs its usual speaker reconnect.

The new `power` component owns the normal-mode button interrupt and standby
sequence:

1. Configure GPIO33 as an input with its internal pull-up and a falling-edge
   interrupt.
2. Deliver the interrupt to a dedicated power task. After 50 ms, accept it
   only if GPIO33 remains low. This debounces a physical press and rejects a
   transient low level.
3. Show amber for 500 ms, stop audio and request Bluetooth shutdown, and wait
   up to 10 seconds. This covers the known ~5.3-second JBL A2DP opening delay
   while still guaranteeing a stuck radio cannot prevent standby indefinitely.
4. Put the LIS2DW12 into its power-down mode, turn the LED fully off, wait for
   GPIO33 to return high, arm EXT0 wake on GPIO33 low, and enter deep sleep.

Waiting for release prevents the original press from satisfying the level-based
EXT0 wake condition and producing a boot loop. The debounce plus internal
pull-up means an unfitted button does not request standby.

## Component Boundaries

`components/power` exposes `power_init()` to start the GPIO33 interrupt and
power task. Its only responsibility is translating a deliberate button press
into the ordered standby sequence.

`components/bluetooth` adds a narrow `bluetooth_audio_shutdown(timeout_ms)`
lifecycle API. It stops PCM playback, suppresses fresh connection attempts,
and disconnects the configured speaker. If an A2DP connection is opening, it
waits for the connection callback and then disconnects the established link,
matching the safety rule introduced in task 3. It signals completion on the
disconnect callback; expiry of the timeout is logged and standby continues.
This API must not change `bluetooth_audio_policy.*`, speaker alternation, or
retry mechanics.

`components/accelerometer` adds `accelerometer_prepare_for_sleep()`, which
writes the LIS2DW12 power-down ODR setting before deep sleep. It does not
alter impact classification or calibration sampling behavior.

Although task 4 originally listed only the new `power` component plus main,
configuration, and LED files, these two lifecycle APIs are necessary narrow
extensions: neither clean Bluetooth disconnect nor accelerometer power-down
is currently available to a new component.

## LED Contract

| State | Indication |
| --- | --- |
| Entering standby | Amber for 500 ms |
| In deep sleep | LED power disabled (off) |
| Waking from GPIO33 EXT0 | Cyan for 200 ms, then normal Bluetooth status |

Power-on boot retains the existing boot indication. Existing connecting,
ready, playing, and error colors remain unchanged.

## Reconnect Cadence

Set `APP_BT_RECONNECT_MS` to `2000U`. This changes the normal Bluetooth retry
cadence to two seconds; it does not change the policy's alternation, immediate
failure behavior, or manual-reconnect semantics. The policy tests must assert
the exact new boundary through the application configuration value.

## Calibration Mode

When `CALIBRATION_MODE=1`, `main.c` continues to call only
`calibration_mode_start()`. It neither initializes the power component nor
configures GPIO33, so standby is unsupported as required.

## Failure Handling

GPIO setup or power-task creation failure is logged and shown as the existing
error LED state. A Bluetooth-shutdown timeout is logged but does not block
deep sleep indefinitely. Deep sleep is still safe because it stops the CPU and
radio; on wake, initialization starts from a clean boot.

## Verification

Automated tests cover the accepted/debounced button transition and verify that
the reconnect policy observes the two-second configuration boundary without
changing speaker alternation. The existing Unity suite remains green.

Hardware verification records:

- idle-connected and standby battery current, demonstrating at least 5x
  reduction;
- button entry, button wake, speaker reconnect, and a subsequent impact cue;
- 5 minutes of normal use with GPIO33 unconnected, with no standby entry;
- observed amber/off/cyan LED indications and their durations.

The measured current values, final deep-sleep/pull configuration, and LED
results are appended to `.tasks/database.md` after hardware validation.
