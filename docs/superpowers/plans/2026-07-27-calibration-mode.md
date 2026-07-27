# Calibration Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Replace firmware ring-buffer/CSV calibration with an 800 Hz binary calibration firmware and metadata-driven Windows/Android visualizer, including a locally built APK.

**Architecture:** The existing production environment remains separate. A compile-time calibration environment runs a COBS/CRC transport driven by INT2 data-ready events; a Kivy host app owns the 30-second history and graph.

**Tech Stack:** ESP-IDF 5.2.1, FreeRTOS, ESP32 UART/I2C/GPIO, COBS, CRC-32/ISO-HDLC, Python 3.12, pytest, Kivy, pyserial, PyJNIus, usb-serial-for-android 3.11.0, Buildozer, PyInstaller.

## Global Constraints

- Production stays 115200 baud with human-readable diagnostics.
- Calibration defines CALIBRATION_MODE=1, uses UART0 at 921600 8N1, and emits no text after binary transport activation.
- Current accelerometer range, sensitivity, Y axis, threshold, ODR and mode remain authoritative. Calibration firmware and Python must not duplicate them.
- INT1/GPIO14 remains tap detection; INT2/GPIO4 is calibration data-ready. ISRs only notify tasks.
- Actual samples run at 800 Hz; sample path makes no allocation; batches contain at most 16 samples.
- Protocol v1 is COBS encoded, zero terminated, little-endian and CRC-32/ISO-HDLC protected.
- The host retains 30 seconds from metadata and independently reports firmware drops and host sequence gaps.
- Do not add a ring buffer, CSV dump, capture command, or text sample stream.

---

### Task 1: Add Calibration Build and INT2 Data-Ready Interface

**Files:**

- Modify: platformio.ini, src/main.c, components/accelerometer/include/accelerometer.h, components/accelerometer/accelerometer.c
- Create: components/calibration/CMakeLists.txt, components/calibration/include/calibration_mode.h, components/calibration/calibration_mode.c

**Interfaces:**

- Produces void calibration_mode_start(void).
- Produces bool accelerometer_set_data_ready_callback(accelerometer_irq_callback_t callback).
- Existing accelerometer_init INT1 callback contract remains unchanged.

- [ ] **Step 1: Write the failing calibration build configuration**

Add environment tinypico_calibration extending tinypico, with CALIBRATION_MODE=1 and monitor_speed 921600. Add a src/main.c compile-time default:

~~~c
#ifndef CALIBRATION_MODE
#define CALIBRATION_MODE 0
#endif
~~~

- [ ] **Step 2: Build the calibration environment before implementation**

Run:

~~~powershell
pio run -e tinypico_calibration
~~~

Expected: failure because the new environment or calibration entry point does not exist.

- [ ] **Step 3: Implement the production/calibration entry split**

In calibration mode app_main calls calibration_mode_start only. In normal mode preserve the current battery, impact, diagnostics and audio start path.

- [ ] **Step 4: Implement INT2 notification-only ISR setup**

Configure GPIO4 positive-edge interrupt only for calibration sampling. Implement:

~~~c
static void IRAM_ATTR data_ready_isr(void *arg) {
    (void)arg;
    if (s_data_ready_callback != NULL) s_data_ready_callback();
}
~~~

The callback performs no I2C, UART, logging, allocation, or packet formatting.

- [ ] **Step 5: Verify both firmware images**

Run:

~~~powershell
pio run -e tinypico
pio run -e tinypico_calibration
~~~

Expected: both exit zero.

- [ ] **Step 6: Commit**

~~~bash
git add platformio.ini src components/accelerometer components/calibration
git commit -m "feat: add calibration firmware build mode"
~~~

### Task 2: Build the Firmware Protocol with Tests

**Files:**

- Create: components/calibration/include/calibration_protocol.h, components/calibration/calibration_protocol.c
- Create: test/test_calibration_protocol/test_calibration_protocol.c

**Interfaces:**

- Produces calibration_cobs_encode, calibration_cobs_decode, calibration_packet_encode and calibration_packet_decode.
- Produces CAL_PKT_METADATA=1, CAL_PKT_SAMPLE_BATCH=2, CAL_PKT_STATUS=3, CAL_PKT_ERROR=4 and CAL_CMD_GET_METADATA=128.
- Produces packed metadata, sample, batch, status and error structures.

- [ ] **Step 1: Write failing Unity tests**

Test COBS round trip with zero payload bytes; CRC32 of 123456789 is 0xCBF43926; metadata round trip; 16 sample batch round trip; CRC failure; payload-length failure; and unsupported version failure.

- [ ] **Step 2: Run the failing test**

~~~powershell
pio test -e tinypico -f test_calibration_protocol
~~~

Expected: compilation failure before protocol implementation.

- [ ] **Step 3: Implement COBS, CRC and strict framing**

Encode an unencoded packet as version, type, uint16 little-endian payload length, payload and uint32 little-endian CRC. COBS encode it and append zero. Reject short frames, version not equal to one, length mismatches and invalid CRC before exposing payload.

- [ ] **Step 4: Re-run protocol test**

Run the Task 2 command. Expected: every test passes.

- [ ] **Step 5: Commit**

~~~bash
git add components/calibration test/test_calibration_protocol
git commit -m "feat: add calibration binary protocol"
~~~

### Task 3: Implement Firmware Sampling and Binary UART Stream

**Files:**

- Create: components/calibration/include/calibration_stream.h, components/calibration/calibration_stream.c
- Modify: components/calibration/calibration_mode.c, components/accelerometer/accelerometer.c
- Create: tools/calibration_stream_smoke.py

**Interfaces:**

- Produces bool calibration_stream_start(void).
- Produces void calibration_stream_on_data_ready_from_isr(void).
- Consumes accelerometer_read_sample only from a FreeRTOS sampling task.

- [ ] **Step 1: Define static sample queue contract**

Use:

~~~c
typedef struct __attribute__((packed)) {
    uint32_t seq, timestamp_us;
    int16_t x_raw, y_raw, z_raw;
} calibration_sample_t;
~~~

Create a 128-element FreeRTOS queue at start. Maintain samples_acquired, samples_transmitted and samples_dropped.

- [ ] **Step 2: Implement the sample task**

On each INT2 task notification, read XYZ, use lower uint32 of esp_timer_get_time, increment sequence and do non-blocking xQueueSend. A full queue increments samples_dropped. No allocation or UART write occurs in this task.

- [ ] **Step 3: Implement transport and command tasks**

Set UART0 to 921600, send metadata at activation, batch up to 16 samples, send status every second, and parse COBS input. A valid empty CAL_CMD_GET_METADATA returns metadata; invalid commands map to a binary error packet.

Metadata must originate from app_config and the shared full-scale sensitivity mapping: sample rate 800, selected Y axis, active raw threshold, GPIO14 and GPIO4.

- [ ] **Step 4: Build, flash and smoke-test COM4**

~~~powershell
pio run -e tinypico_calibration
pio run -e tinypico_calibration -t upload --upload-port COM4
python tools/calibration_stream_smoke.py --port COM4 --seconds 30
~~~

Expected: valid metadata, sample batches and status; smoke test reports malformed frames, firmware drops and host gaps separately.

- [ ] **Step 5: Commit**

~~~bash
git add components/calibration components/accelerometer tools/calibration_stream_smoke.py
git commit -m "feat: stream calibration samples over binary UART"
~~~

### Task 4: Add Pure-Python Protocol Parser and Tests

**Files:**

- Create: calibration_app/app/__init__.py, cobs.py, crc.py, protocol.py, reader.py
- Create: calibration_app/tests/test_cobs.py, test_crc.py, test_protocol.py
- Create: calibration_app/requirements.txt

**Interfaces:**

- Produces encode_frame(packet_type: int, payload: bytes) -> bytes.
- Produces FrameParser.feed(data: bytes) -> list[Packet].
- Packet has version, packet_type and payload fields.

- [ ] **Step 1: Write failing pytest cases**

Cover zero-containing COBS payload, known CRC, all four device packet types, invalid CRC, invalid length, unsupported version, byte-by-byte frames, multiple frames in one read, and malformed then valid recovery.

- [ ] **Step 2: Run failing tests**

~~~bash
python3 -m pytest calibration_app/tests/test_cobs.py calibration_app/tests/test_crc.py calibration_app/tests/test_protocol.py -q
~~~

Expected: imports fail because the modules do not exist.

- [ ] **Step 3: Implement parser**

Use zlib.crc32 and no COBS dependency. FrameParser buffers arbitrary reads, splits on zero only, increments bad_packet_count for an invalid complete frame, and resumes with the next frame.

- [ ] **Step 4: Verify and commit**

~~~bash
python3 -m pytest calibration_app/tests/test_cobs.py calibration_app/tests/test_crc.py calibration_app/tests/test_protocol.py -q
git add calibration_app
git commit -m "feat: add calibration host protocol parser"
~~~

### Task 5: Add Metadata Model and Spike-Preserving Graph Data

**Files:**

- Create: calibration_app/app/model.py, calibration_app/app/graph.py
- Create: calibration_app/tests/test_model.py, test_downsampling.py

**Interfaces:**

- Produces CalibrationModel.apply_metadata, append_samples and graph_config.
- Produces min_max_downsample(points, width).

- [ ] **Step 1: Write failing metadata tests**

Metadata A: 4 g, X, threshold 1024, sensitivity 0.976, 400 Hz must yield range minus four through four, X, 12000 samples and plus/minus 0.999424 g. Metadata B: 16 g, Z, threshold 4096, sensitivity 1.952, 800 Hz must immediately yield Z, 24000 and updated threshold without source constants.

- [ ] **Step 2: Write failing downsampling test**

Generate 30 seconds of low-amplitude data and one positive and one negative single-sample impact. Downsample to a much smaller width and assert both spikes remain.

- [ ] **Step 3: Implement model and bins**

Retain raw sequence/timestamp/X/Y/Z in a deque of round(sample_rate * 30). Detect gaps, duplicates, out-of-order samples and uint32 wrap in host_sequence_gaps. Render conversion only derives g. Divide data into width bins, retaining ordered minimum and maximum per bin.

- [ ] **Step 4: Verify and commit**

~~~bash
python3 -m pytest calibration_app/tests/test_model.py calibration_app/tests/test_downsampling.py -q
git add calibration_app
git commit -m "feat: add metadata-driven calibration model"
~~~

### Task 6: Add Kivy Windows Application

**Files:**

- Create: calibration_app/main.py, calibration_app/calibration.kv
- Create: calibration_app/app/controller.py, calibration_app/serial_backends/base.py, windows_serial.py
- Create: calibration_app/tests/test_windows_serial.py, calibration_app/calibration.spec

**Interfaces:**

- Produces SerialDevice, SerialBackend, WindowsSerialBackend and CalibrationController.

- [ ] **Step 1: Write serial backend test**

Mock serial.tools.list_ports.comports to return CH9102 and assert a device label of COM4 — USB-Enhanced-SERIAL CH9102. Assert connect opens COM4 at 921600 8N1.

- [ ] **Step 2: Implement background serial reader**

The reader thread calls FrameParser.feed and queues decoded packets. The Kivy thread drains them at 20 Hz and sends CAL_CMD_GET_METADATA after connecting. The UI thread never performs a blocking serial read.

- [ ] **Step 3: Implement metadata-driven Canvas graph**

Show connection controls, metadata, firmware drops, host gaps and bad frames. Draw only the metadata-selected axis, plus/minus range, and symmetric derived thresholds using Task 5 min/max points.

- [ ] **Step 4: Package Windows distribution**

Create a PyInstaller spec and build a self-contained dist/CaneCalibration application.

- [ ] **Step 5: Commit**

~~~bash
git add calibration_app
git commit -m "feat: add Windows calibration visualizer"
~~~

### Task 7: Add Android USB Backend and Locally Build APK

**Files:**

- Create: calibration_app/serial_backends/android_usb_serial.py, calibration_app/buildozer.spec, calibration_app/README.md
- Modify: calibration_app/main.py, calibration_app/requirements.txt

**Interfaces:**

- Produces AndroidUsbSerialBackend with the SerialBackend contract.

- [ ] **Step 1: Bootstrap WSL toolchain**

Install Java 17, Python build prerequisites, Buildozer, Android command-line SDK, NDK and Gradle in WSL. Record exact commands and ANDROID_HOME/ANDROID_SDK_ROOT paths in README.

- [ ] **Step 2: Implement USB Host flow**

Use PyJNIus to enumerate UsbManager devices and usb-serial-for-android drivers, request permission, configure 921600 8N1, and map permission denial, detach, reattach and reads errors to disconnected/error controller state. Include Gradle dependency com.github.mik3y:usb-serial-for-android:3.11.0 and android.hardware.usb.host.

- [ ] **Step 3: Configure Buildozer**

Include python3, kivy and pyjnius. Set application name CaneCalibration and include the serial backend/application modules.

- [ ] **Step 4: Build APK**

~~~bash
cd /mnt/m/CodingProjects/RenoirCane/.worktrees/cane-impact-audio/calibration_app
buildozer android debug
~~~

Expected: bin/CaneCalibration-*-debug.apk exists. Copy it to dist/CaneCalibration.apk without committing generated output.

- [ ] **Step 5: Commit**

~~~bash
git add calibration_app
git commit -m "feat: add Android calibration package"
~~~

### Task 8: Verify and Remove CSV Scope

**Files:**

- Modify: components/debug_console/debug_console.c
- Create: docs/calibration-verification.md

- [ ] **Step 1: Remove capture command**

Delete capture dump from help and dispatch. Do not add accel_capture.

- [ ] **Step 2: Run full static verification**

~~~powershell
pio run -e tinypico
pio run -e tinypico_calibration
python -m pytest calibration_app/tests -q
~~~

Expected: all commands exit zero.

- [ ] **Step 3: Run hardware and package checks**

Flash calibration firmware and run 30-second COM4 smoke test. Flash normal firmware and run status plus audio play. Record actual counters, APK path and checksum in docs/calibration-verification.md. If no Android OTG device is available, mark runtime attach and 10-minute test pending without claiming completion.

- [ ] **Step 4: Commit**

~~~bash
git add components/debug_console calibration_app docs
git commit -m "docs: verify calibration transport workflow"
~~~

