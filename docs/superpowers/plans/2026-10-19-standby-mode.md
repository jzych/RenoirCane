# Low-Power Standby Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Enter low-power deep sleep from the optional GPIO33 switch, then wake and return to normal Bluetooth operation.

**Architecture:** A new `power` component debounces the active-low GPIO33 button and sequences LED, Bluetooth, accelerometer, and ESP-IDF deep-sleep calls. Bluetooth and accelerometer receive narrow lifecycle APIs; the Bluetooth policy remains unchanged. `app_main` invokes the power component only outside calibration mode.

**Tech Stack:** ESP-IDF 5.2, FreeRTOS, ESP32 deep sleep/EXT0 wake, Unity, PlatformIO.

**Spec:** `docs/superpowers/specs/2026-10-19-standby-mode-design.md`

## Global Constraints

- GPIO33 is active-low, connected to GND when pressed, and uses its internal pull-up.
- A press is valid only when the pin is still low after 50 ms; release before deep sleep prevents EXT0 wake loops.
- Use deep sleep, EXT0 wake on GPIO33 low, amber 500 ms on entry, LED off in standby, and cyan 200 ms after an EXT0 wake.
- Set `APP_SLEEP_BUTTON_DEBOUNCE_MS` to `50U`, `APP_SLEEP_BT_SHUTDOWN_TIMEOUT_MS` to `10000U`, `APP_SLEEP_ENTER_LED_MS` to `500U`, and `APP_SLEEP_WAKE_LED_MS` to `200U`.
- Calibration mode must not configure or start standby handling.
- `APP_BT_RECONNECT_MS` is exactly `2000U`; do not change speaker alternation, manual reconnect semantics, or `bluetooth_audio_policy.*`.
- Bluetooth shutdown may time out and log before sleeping, but must suppress fresh connection attempts and stop PCM playback.
- Record actual current and hardware-validation results only after measurement; never invent values.

## Review Focus

- GPIO33 high at the post-debounce sample must not enter standby; Task 1 adds this Unity test.
- A valid press must be reported once only; Task 1 verifies the pending debounce state is consumed.
- A held first press must not wake the device immediately; Task 3's hardware check holds then releases before sleep.
- A Bluetooth connection still opening at shutdown must not cause a duplicate connect or a reconnect; Task 2 is exercised with the existing task-3 Unity regressions and Task 4 hardware check.
- GPIO33 unconnected must remain high through normal use; Task 4 includes a timed hardware observation and serial-log capture.

---

### Task 1: Add power configuration and a testable button debounce primitive

**Files:**

- Modify: `components/app_config/include/app_config.h`
- Create: `components/power/CMakeLists.txt`
- Create: `components/power/include/power.h`
- Create: `components/power/include/power_button.h`
- Create: `components/power/power_button.c`
- Modify: `src/CMakeLists.txt`
- Modify: `test/test_calibration_protocol/test_calibration_protocol.c`

**Interfaces:**

- Produces `void power_button_init(power_button_t *button)`, `void power_button_note_falling_edge(power_button_t *button)`, and `bool power_button_confirm_press(power_button_t *button, bool pin_is_low)` for the GPIO task.
- Produces the `power_init(void)` declaration for Task 3.
- Adds `APP_SLEEP_BUTTON_GPIO`, `APP_SLEEP_BUTTON_DEBOUNCE_MS = 50U`, `APP_SLEEP_BT_SHUTDOWN_TIMEOUT_MS = 10000U`, `APP_SLEEP_ENTER_LED_MS = 500U`, and `APP_SLEEP_WAKE_LED_MS = 200U`; changes `APP_BT_RECONNECT_MS` to `2000U`.

- [x] **Step 1: Write failing Unity tests for exact reconnect cadence and debounced button acceptance**

Add `test_bluetooth_reconnect_interval_is_two_seconds` asserting `APP_BT_RECONNECT_MS == 2000U`. Add `test_power_button_requires_a_low_post_debounce_sample_and_consumes_the_press`: after a falling edge, a high confirmation returns false; after another falling edge, a low confirmation returns true once and false if confirmed again.

- [x] **Step 2: Run test to verify it fails**

Run: `& 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' test -e tinypico_test --without-uploading --without-testing`

Expected: compilation fails because `power_button.h` and its functions do not yet exist; the reconnect assertion would fail with the current 5000 ms value.

- [x] **Step 3: Implement the configuration and debounce primitive**

Define `power_button_t` with one `bool debounce_pending` field. `power_button_note_falling_edge` sets that field; `power_button_confirm_press` clears it and returns true only for a pending, low sample. Define the button GPIO, 50 ms debounce, 10-second Bluetooth shutdown timeout, 500 ms entry indication, 200 ms wake indication, and the 2000 ms reconnect cadence in `app_config.h`. Register the `power` component and make `src` require it.

- [x] **Step 4: Run test to verify it passes**

Run: `& 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' test -e tinypico_test --without-uploading --without-testing`

Expected: `test_calibration_protocol` builds successfully and includes both new tests.

- [x] **Step 5: Commit**

```powershell
git add components/app_config/include/app_config.h components/power src/CMakeLists.txt test/test_calibration_protocol/test_calibration_protocol.c
git commit -m "feat: add standby button configuration"
```

### Task 2: Add shutdown lifecycle APIs without changing Bluetooth policy

**Files:**

- Modify: `components/bluetooth/include/bluetooth_audio.h`
- Modify: `components/bluetooth/bluetooth_audio.c`
- Modify: `components/accelerometer/include/accelerometer.h`
- Modify: `components/accelerometer/accelerometer.c`
- Modify: `test/test_calibration_protocol/test_calibration_protocol.c`

**Interfaces:**

- Consumes the timeout configuration from Task 1.
- Produces `bool bluetooth_audio_shutdown(uint32_t timeout_ms)` and `void accelerometer_prepare_for_sleep(void)` for Task 3.

- [x] **Step 1: Extend the Bluetooth regression with the final two-second boundary**

In `test_bluetooth_policy_reconnects_at_the_exact_delay_boundary_after_disconnect`, retain the one-microsecond-before/exact-boundary assertions and assert that `RECONNECT_DELAY_US` equals `2000000`. This test is the policy regression guard; it must pass before lifecycle code is changed.

- [x] **Step 2: Run test to establish the Task 1 baseline**

Run: `& 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' test -e tinypico_test --without-uploading --without-testing`

Expected: the exact-delay assertion passes only with Task 1's 2000 ms configuration, and all task-3 reconnect regressions remain green.

- [x] **Step 3: Implement shutdown and accelerometer power-down**

Implement `bluetooth_audio_shutdown(uint32_t timeout_ms)` in `bluetooth_audio.c` with a module-local shutdown flag and a completion notification. It must call `audio_stop`, prevent `attempt_connection` and media keep-alive from starting more work, disconnect an established target, and defer that disconnect from an opening link until its connected callback. The disconnected callback wakes the waiting shutdown caller without altering `bluetooth_audio_policy.*`. On no active link or timeout, return promptly with a boolean completion result. Implement `accelerometer_prepare_for_sleep()` by disabling its interrupt path and writing the LIS2DW12 ODR power-down value (`0x00` in CTRL1).

- [x] **Step 4: Run the Unity and normal-firmware builds**

Run: `& 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' test -e tinypico_test --without-uploading --without-testing; & 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' run -e tinypico`

Expected: Unity builds with all task-3 reconnect regressions and the new 2-second boundary; normal firmware builds with no policy-source modification.

- [x] **Step 5: Commit**

```powershell
git add components/bluetooth components/accelerometer test/test_calibration_protocol/test_calibration_protocol.c
git commit -m "feat: add standby shutdown lifecycle APIs"
```

### Task 3: Implement the runtime standby sequence and wake indication

**Files:**

- Create: `components/power/power.c`
- Modify: `components/status_led/include/status_led.h`
- Modify: `components/status_led/status_led.c`
- Modify: `components/status_led/CMakeLists.txt`
- Modify: `src/main.c`
- Modify: `src/CMakeLists.txt`

**Interfaces:**

- Consumes `power_button_*`, `bluetooth_audio_shutdown`, and `accelerometer_prepare_for_sleep`.
- Implements the public `void power_init(void)` and `bool power_woke_from_button(esp_sleep_wakeup_cause_t cause)` used by normal-mode `app_main`.

- [x] **Step 1: Write failing Unity test for wake-cause classification**

Add `test_power_wake_cause_identifies_only_ext0_wake`, asserting that `power_woke_from_button(ESP_SLEEP_WAKEUP_EXT0)` is true and `power_woke_from_button(ESP_SLEEP_WAKEUP_UNDEFINED)` is false.

- [x] **Step 2: Run test to verify it fails**

Run: `& 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' test -e tinypico_test --without-uploading --without-testing`

Expected: compilation fails because `power_woke_from_button` does not yet exist.

- [x] **Step 3: Implement `power_init()` and wire normal-mode startup**

Configure GPIO33 input/pull-up/falling-edge interrupt, reuse the existing ISR service safely, and create a power task. The task waits 50 ms after an edge, samples `gpio_get_level`, and proceeds only when `power_button_confirm_press` accepts it. It sets the new amber standby LED state for 500 ms, calls Bluetooth shutdown and accelerometer prepare-for-sleep, powers the LED off, waits until GPIO33 is high, calls `esp_sleep_enable_ext0_wakeup(APP_SLEEP_BUTTON_GPIO, 0)`, and invokes `esp_deep_sleep_start()`.

Implement `power_woke_from_button` as the EXT0-only predicate. In normal-mode `app_main`, initialize the LED first; when the predicate accepts `esp_sleep_get_wakeup_cause()`, display the new cyan waking state for 200 ms before normal initialization. Call `power_init()` only on the normal-mode path after required services initialize. Do not modify `calibration_mode_start()`.

- [x] **Step 4: Build both normal and calibration firmware configurations**

Run: `& 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' run -e tinypico -e tinypico_calibration`

Expected: both environments finish `SUCCESS`; calibration has no GPIO33/power-task initialization path.

- [x] **Step 5: Commit**

```powershell
git add components/power components/status_led src/main.c src/CMakeLists.txt
git commit -m "feat: enter standby from GPIO33 button"
```

### Task 4: Verify device behavior and record measured results

**Files:**

- Modify: `.tasks/database.md`
- Modify: `.tasks/state.md`

**Interfaces:**

- Consumes the flashed `tinypico` firmware from Task 3 and a GPIO33-to-GND momentary switch.
- Produces recorded measurements and a completed task-state entry.

- [ ] **Step 1: Flash normal firmware and run automated regression checks**

Run: `& 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' run -e tinypico -t upload --upload-port COM4; & 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' test -e tinypico_test --upload-port COM4 --test-port COM4`

Expected: the Unity device suite passes; reflash `tinypico` after the test image if the test runner leaves it installed.

- [ ] **Step 2: Perform and record the required hardware checks**

Measure idle-connected and standby current at the battery; record both values and their ratio, demonstrating at least a 5x reduction. Verify amber entry, LED-off sleep, cyan wake, speaker reconnection, and an audible impact cue. With GPIO33 unconnected, observe normal operation for 5 minutes and record that no standby was entered. Repeat once with a held press to confirm release-before-sleep avoids an immediate wake loop.

- [ ] **Step 3: Append only observed decisions and measurements to the task database**

Add the deep-sleep/EXT0-low/internal-pull-up decision, three LED patterns, `APP_BT_RECONNECT_MS = 2000U`, observed current values/ratio, and each hardware-check outcome to `.tasks/database.md`. Set `in_progress: none`, `last_completed: 4`, and the completed branch in `.tasks/state.md` only after all acceptance criteria pass.

- [x] **Step 4: Run final non-device verification**

Run: `& 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' run -e tinypico -e tinypico_calibration; & 'C:\Users\jzych\.platformio\penv\Scripts\pio.exe' test -e tinypico_test --without-uploading --without-testing`

Expected: both firmware builds and the Unity build finish successfully. Run the audio host suite from its configured Python environment if it is available outside the ignored worktree virtual environment; report a missing environment rather than changing project dependencies.

- [ ] **Step 5: Commit**

```powershell
git add .tasks/database.md .tasks/state.md
git commit -m "docs: record standby validation"
```

## Execution record (2026-10-19)

Tasks 1-3 are implemented on `feature/task-4-standby-mode`. Normal and
calibration builds passed. The host runner passed the complete 35-test Unity
suite and dedicated power/Bluetooth/sensor behavior checks; the six audio
conversion tests passed using standard-library unittest.

Execution refinements:

- Replaced bare configuration assertions with the observable 1,999,999 /
  2,000,000 microsecond retry boundary.
- Used real power-task sequence tests instead of adding a trivial EXT0
  predicate API; `app_main` directly reads ESP-IDF's wake cause.
- Added separate physical-link tracking and worker-owned disconnect requests
  so shutdown also handles an opening link or a live link in media-error state.
- Added stable release gating at startup, RTC pull retention, and LED-off pad
  hold across deep sleep. A held wake press must be released before rearming.
- Added native HAL test harnesses because cross-compilation alone cannot
  execute assertions. On this Windows machine they run through Ubuntu/WSL;
  Windows clang lacks an installed C runtime.
- Cleaned the generated normal/test build environments after finding a stale
  CMake source list that omitted the newly added `power.c`.
- The existing Windows calibration-app venv cannot launch its Microsoft Store
  Python interpreter. No calibration-app source was changed.

Task 4 remains pending physical access: the port inventory contains Bluetooth
and STM32 devices, but no TinyPICO/CH9102. No other device was flashed and no
current measurements or physical button/LED results have been invented.
Decisions are recorded in the shared local `.tasks/database.md` and tracked
`docs/standby-mode.md`. Local task state remains `last_completed: 3`,
`in_progress: 4`, with the feature branch named. Merge requires explicit
user consent.

Final review found no actionable defects in the implementation range
`8c34fad..b51396d`. The regenerated Unity device image also built successfully.
Physical acceptance remains pending. Host tests simulate hardware and scheduling;
they do not establish measured current or actual dual-core timing. On a final
RTC setup error the firmware logs that reset is required. Existing Bluetooth
API-error recovery remains outside task 4. Deep sleep remains the approved
bounded fallback after disconnect timeout.
