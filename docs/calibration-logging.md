# Calibration UART logging

Calibration mode owns UART0 at 921600 baud. Runtime `ESP_LOG*`, stdout/stderr
(`printf`, `puts`, etc.), and ROM print-channel output enter a bounded text
buffer; only the calibration transport task writes the UART. Normal-mode
console output at 115200 is unchanged.

Calibration mode does not start the normal standby task: the cane's GPIO33 OFF
switch is inactive while this firmware runs. Use the **speaker's** power button
for reconnect testing. Normal standby behavior returns with normal firmware.

Protocol version remains **2**. `CAL_PKT_LOG` (`0x07`) contains up to 256 text
bytes without a terminating NUL. Existing packet layouts, including the binary
`CAL_PKT_ERROR` payload, are unchanged. The app forwards text through its
existing `CaneDiag` diagnostics: Windows logging output or Android logcat. There
is no new on-screen log panel. The incremental UTF-8 decoder tolerates split
characters and replaces invalid bytes; session reset clears its pending state.

## Buffering and startup

- Capture starts before Bluetooth initialization. The 4 KiB buffer uses internal
  RAM and interrupt-safe locking; its ROM character callback is IRAM-safe.
- Formatting is bounded to 256 bytes per ESP-IDF log call. Long messages end in
  `[truncated]`. Overflow drops new bytes, and a framed dropped-byte notice
  reports both truncation and buffer loss. No producer waits for transport.
- The transport sends at most one log packet per iteration, after samples,
  impact events and due telemetry. Overflow notices share that packet with
  queued text so continuous overflow cannot prevent the buffer from draining.
- Pending stdio is flushed before redirection. UART TX completion is checked
  with a 1000 ms timeout before changing baud. A delimiter and metadata precede
  streamed data. Setup failure stops binary startup and selects the error LED.
- ESP-IDF presents `/` when the VFS mount point is opened. Capture hooks are
  installed before reopening the shared standard streams, keeping ESP-IDF logs
  safe even if a reopen fails.

ROM and bootloader text emitted before application initialization remains a boot
preamble at 115200. A host listening at 921600 can count that preamble as invalid
frames. Discard/synchronize startup first and record its errors separately;
the production frame parser remains strict throughout the binary session.

Firmware-log forwarding failures are contained in the controller. The reader's
pre-existing, separate batch-diagnostics failure behavior is unchanged: if both
of those diagnostic backends fail, the reader can close the connection before
dispatching packets. That behavior is outside task 6's permitted app files.

## Acceptance check

After receiving valid metadata and baseline impact telemetry, capture at least
600 seconds with three completed speaker disconnect/reconnect cycles and several
impacts. Require zero new bad frames or bad payloads. For each pair of telemetry
packets, compare the `event_packets_sent` delta against **all** processed/skipped
impact packets received between those boundaries. Check sample/event queue drops
and host sample gaps separately. Restore normal firmware and verify its usual
115200 console after calibration testing.

Automated coverage includes capture/transport startup order, initialization
failure, VFS mount translation, concurrent producers, truncation, ring overflow
and wrap, UART drain failure, mixed packet priority/event accounting, UTF-8
fragmentation/reset and controller diagnostic failure. The native regression
suites, 43 Unity cases, 68 app tests and both firmware builds passed; the Unity
device image compiled without upload or device execution.

Task 5's physical impact-detection acceptance is separate.
