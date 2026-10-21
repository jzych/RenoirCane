# Bluetooth recovery checks

Run from the repository root on Linux/WSL:

```sh
python3 tools/test_standby.py
python3 components/bluetooth/test/test_recovery.py
```

For the device Unity suite, add `-DUNITY_SUPPORT_64` to the test build flags
for both PlatformIO's Unity library and the ESP-IDF test sources; existing
impact timestamp assertions require it. Then run `python -m platformio test
-e tinypico_test -f test_calibration_protocol --upload-port COM4 --test-port
COM4` and restore `tinypico` firmware afterward. The ESP-IDF menuconfig option
alone does not configure PlatformIO's separately built Unity library.

The Unity suite covers the exact retry boundary, speaker alternation,
clearing a pending manual reconnect/media operation, and terminal fatal
errors. The adapter test injects failed disconnects, rejected media commands
and acknowledgements, unexpected profile status, missing initialization
completion, and completion racing a profile retry. It also checks shutdown
suppression and late connection callbacks. Only SDK/RTOS calls are replaced;
the Bluetooth policy, adapter, and PCM playback code are real.

Recoverable errors stop cue playback, clear stale manual reconnect/media
state, and wait `APP_BT_RECONNECT_MS`. The configured value is currently
2000 ms (the task-4 user override of the original 5-second cadence).
Speaker alternation is retained. Existing links are disconnected before
issuing another connect; a late disconnect does not restart the delay.
Failed cleanup requests wait another full interval before trying again.
Queued disconnects have a 10-second completion deadline. If it expires,
the worker requests A2DP source profile deinitialization: in IDF 5.2.1,
the CLOSING state ignores repeated disconnect commands after a lost close.
Only `ESP_A2D_DEINIT_SUCCESS` permits clearing the old profile/link state;
a late DISCONNECTED callback alone cannot complete profile cleanup.
The source is initialized again after the configured retry delay, then
normal speaker alternation resumes. Cleanup requests that fail immediately
retry after `APP_BT_RECONNECT_MS`; accepted requests without completion
retry after 10 seconds. Playback/connect/init stay gated during cleanup,
and standby suppresses this recovery work.

ESP-IDF 5.2.1 queues profile initialization before reporting success. An
asynchronous allocation/startup failure can omit the completion callback;
there is no explicit `ESP_A2D_INIT_FAIL` event in this SDK. A completion
deadline therefore covers both initial startup and profile retries.
Only the A2DP source profile is retried. Controller/Bluedroid initialization
and other synchronous startup failures remain fatal; manual reconnect and
late events cannot turn those fatal errors into automatic retries.

Hardware acceptance: inject one failed `esp_a2d_source_disconnect()` during
a requested reconnect, then verify automatic cleanup and CONNECTING ->
CONNECTED after the configured delay with no second manual command.
Restore the normal firmware after removing fault injection.

On 2026-10-21, this check passed on TinyPICO COM4: one forced failure at
65617 ms, automatic cleanup/new connection at 67727 ms (2110 ms gap),
then automatic primary reconnection at 80207 ms after an unavailable
secondary timed out. No additional reconnect command was sent.
The full 47-test Unity suite also passed on the device with 64-bit support
enabled in both Unity builds.
