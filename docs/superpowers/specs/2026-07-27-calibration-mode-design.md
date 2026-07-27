# Calibration Mode Design

## Purpose

Replace the deferred firmware-side accelerometer ring buffer and CSV dump with a dedicated calibration build and a host visualizer. The calibration build streams all three LIS2DW12 axes at 800 Hz as a machine-readable binary protocol; the host owns the rolling 30-second history and live graph.

The source requirements are `.tasks/calibration.md`. Normal `tinypico` firmware remains a human-readable, impact-detection build and must not inherit calibration transport behaviour.

## Build Boundary

`platformio.ini` exposes two environments:

- `tinypico`: the existing production firmware at 115200 baud.
- `tinypico_calibration`: extends production settings, defines `CALIBRATION_MODE=1`, and uses 921600 baud.

The application entry point selects the mode at compile time. Calibration startup configures the same LIS2DW12 range, ODR, impact axis, and threshold constants used by production. Once the binary protocol is activated, it writes no text logs to UART0.

## Firmware Architecture

The new `calibration` component has three responsibilities:

1. `calibration_protocol` owns COBS framing, CRC-32/ISO-HDLC, the version-1 packet header, and packet encoding/decoding. It has no GPIO, I2C, UART, or task dependencies.
2. `calibration_stream` owns the static FreeRTOS queue, fixed batches of 16 samples, UART0 writes, status/error reporting, and metadata command handling. No heap allocation occurs after initialization in the sample path.
3. `calibration_mode` creates the data-ready task and transport task. It obtains raw samples through the existing accelerometer component.

The accelerometer component gains a second, minimal callback for INT2 data-ready. INT1/GPIO14 continues to notify the production tap/impact task. In calibration mode, the INT2/GPIO4 ISR only wakes the sample task; that task performs the I2C read, timestamps and sequences the sample, and sends it to the queue. A full queue increments `samples_dropped` without blocking acquisition.

Metadata is constructed from `app_config.h`, never calibration-only duplicate constants. The current build therefore reports 800 Hz, plus/minus 16 g, Y axis, raw threshold 6000, INT1 14, and INT2 4. Sensitivity is exported from one shared conversion helper based on the selected full-scale range.

## Wire Protocol

Every unencoded packet has a version byte, type byte, little-endian payload length, payload, then little-endian CRC32 over the header and payload. COBS encodes that packet and a zero byte terminates it. Version is `1`.

The device transmits a metadata packet at protocol activation and in reply to `CAL_CMD_GET_METADATA`. It emits sample batches of at most 16 timestamped XYZ samples, and status once per second. The host may discard malformed frames; malformed input commands result in a binary error packet. No text, CSV, JSON, or newline framing is permitted in calibration mode.

## Host Application

`calibration_app` keeps protocol/model code independent of Kivy and serial APIs so it can be tested on WSL. A reader thread accumulates serial bytes, splits COBS frames, validates and decodes them, then transfers packet events to the Kivy UI thread through a thread-safe queue.

The model consumes metadata as its only configuration source. It retains raw X/Y/Z samples for `round(sample_rate_hz * 30)` entries and derives graph range, selected axis, and symmetric thresholds at render time. The Canvas graph refreshes at 20 Hz and uses min/max bins rather than every-N decimation, so short impacts remain visible.

Windows uses pyserial enumeration and 921600 8N1. Android uses a `usb-serial-for-android` bridge exposed to Python through PyJNIus, with USB-host permission, detach, and reconnect handling. Buildozer packages `CaneCalibration.apk`; PyInstaller packages the Windows application.

## Failure Handling and Verification

Accelerometer initialization, I2C reads, full queues, UART writes, and invalid host commands map to binary error/status packets after transport activation. The normal firmware keeps its existing logs and console.

Unit tests cover COBS, CRC, protocol validation, parser boundary recovery, sequence tracking, metadata-driven model values, and spike-preserving downsampling. Firmware builds are verified for both environments. Hardware checks cover the calibration stream on COM4; local APK production is required, while Android USB/OTG runtime verification remains dependent on a connected Android host device.
