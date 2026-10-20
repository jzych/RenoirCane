# Task 6: framed calibration logs

Approved in conversation: option (b), existing host diagnostic output, acceptance
window after startup synchronization. Based on main e62e148.

## Contract

- Preserve protocol version 2 and all existing payloads; add CAL_PKT_LOG=0x07
  with up to 256 text bytes, no terminating NUL.
- Capture ESP_LOG, stdout/stderr and runtime ROM printing before Bluetooth.
  Use a static 4096-byte ring, bounded formatting and an IRAM-safe ROM callback.
  Never write UART or wait for transport in producers. Report dropped bytes.
- Initialize impact state and transport before Bluetooth; enable sensor IRQs last.
  Drain old UART output (1000 ms timeout) before switching to 921600; delimiter
  and metadata precede other packets. Sole writer remains the transport task.
  Send at most one log packet per iteration after samples/events/telemetry.
- Forward log payloads to diagnose() using an incremental UTF-8 replacement
  decoder. Reset decoder with session; diagnostic failure is nonfatal.
- Production changes limited to calibration component and app protocol/controller.
  Tests and test wiring may change. Normal-mode logging remains unchanged.

## Tasks

1. Add failing startup, UART, framing, buffer and host regressions; implement
   bounded logging capture and host handling; observe RED then GREEN.
2. Run native regressions, all app tests, both firmware builds and Unity image
   build; independently review the complete delta and address important defects.
3. Hardware: after boot synchronization, observe 600 seconds with >=3 speaker
   disconnect/reconnect cycles. Require zero new bad frames/payloads and compare
   all received processed/skipped markers against matching event_packets_sent
   telemetry deltas. Report startup noise separately. Check normal output at
   115200; restore normal firmware after calibration testing.

## Review focus

Concurrent/ISR/cache-disabled producers, stdio shared across existing/new tasks,
startup failure without raw fallback, overflow fairness and recursive logs from
UART errors, host Unicode fragmentation and diagnostic sink failures.

Task 5 physical acceptance is separate. No merge or push is requested.
