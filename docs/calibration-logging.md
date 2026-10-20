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
- Log delivery is best effort. If a UART write fails or is short, its consumed
  text bytes are counted as lost and its unsent loss count is restored, adding
  to any new producer losses. A later successful log packet reports the total,
  saturating at `UINT32_MAX`; repeatedly failed notices do not inflate it.
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

The shared `diagnose()` adapter contains exceptions from both Android logging
and its Python logging fallback. If both fail, it drops the diagnostic without
recursively logging the failure. Reader regressions cover decoded-packet and
bad-frame diagnostics: valid packets still dispatch, the backend is not closed
and `reader.error` remains unset. Actual serial read failures still close the
backend and report a reader error. This review follow-up was explicitly
approved, expanding the original app-file scope to the shared adapter and its
regression tests.

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
and wrap, UART drain failure, failed/short log writes and loss-count saturation,
mixed packet priority/event accounting, UTF-8
fragmentation/reset, controller diagnostic failure and reader diagnostic sink
failures. The native regression suites, 43 Unity cases, 70 app tests and both
firmware builds passed; the Unity device image compiled without upload or
device execution.

Task 5's physical impact-detection acceptance is separate.

## Hardware observations (2026-10-20)

TinyPICO on COM4, implementation `aa45032`, verified upload hashes:

| Measurement | Baseline | Reconnect follow-up |
| --- | ---: | ---: |
| Measured duration | 600.158 s | 180.121 s |
| Completed reconnect cycles wholly inside the window | 0 | 3 |
| New bad frames / bad payloads | 0 / 0 | 0 / 0 |
| Sample drops / event queue drops / host sequence gaps | 0 / 0 / 0 | 0 / 0 / 0 |
| Impact packets matching firmware telemetry deltas | 11 / 11 | 0 / 0 |
| Matching consecutive telemetry comparisons | 600 | 180 |

Boot/baud-transition noise accounted for 2812 bad frames before the baseline
synchronized; these are excluded only from the agreed post-startup window.
The follow-up started with no startup errors. Firmware uptime advanced from
601078 ms at the baseline's end to 738078 ms at the follow-up's first status,
then 918078 ms at its end. Four successful reconnect chains were observed in
total; the first spans the gap between captures and three are fully within the
follow-up. These results verify the ten-minute stream and reconnect behavior in
**separate capture windows**, rather than claiming a single uninterrupted
ten-minute capture containing all three cycles.

The user explicitly skipped the remaining combined 600-second/three-reconnect
acceptance test during review follow-up. It remains unverified. These hardware
observations apply to `aa45032`; the subsequent UART failure-accounting fix is
covered by automated fault injection and has not been flashed or live tested.

Raw captures, firmware diagnostic logs, summaries and the reconnect timeline
are retained locally under this worktree's `.pio/task6-*` paths.

Normal firmware was restored on COM4 with verified flash hashes. An eight-second
115200 startup capture confirmed `v1.0-32-gaa45032`, ELF SHA prefix `bdea4ea90`,
threshold 4500, I2C ISR on CPU1 and continuing sensor logs without a reset loop.
Normal standby code and logging call sites are unchanged; the calibration
capture helper is absent from the normal firmware's linked symbols. Serial
capture was closed afterward. No merge or push was performed.
