# Codex Task: Calibration Firmware Mode and Cross-Platform Accelerometer Visualizer

## Goal

Implement a compile-time **calibration mode** for the TinyPICO v3 firmware and a companion Python/Kivy application for Windows and Android.

In calibration mode, the LIS2DW12 runs continuously at **800 Hz** and streams raw accelerometer data over the TinyPICO USB serial interface using a **binary machine-readable protocol only**.

The companion application:

- enumerates available serial devices;
- connects to the selected TinyPICO;
- reads firmware-generated metadata;
- uses that metadata as the single source of truth for graph configuration;
- retains the latest 30 seconds of accelerometer samples;
- displays a live graph of the configured impact-detection axis;
- displays impact threshold lines derived from firmware metadata;
- runs on Windows and Android.

This task replaces the previous firmware-side ring-buffer / CSV-dump calibration design.

---

# Fixed Hardware Configuration

```text
Board                    TinyPICO v3 / ESP32
Accelerometer            LIS2DW12 / SEN0405
I2C address              0x19

I2C SDA                  GPIO21
I2C SCL                  GPIO22

LIS2DW12 INT1            GPIO14
LIS2DW12 INT2            GPIO4

TinyPICO USB-UART        onboard CH9102F
Calibration baud rate    921600
```

The existing impact-detection firmware configuration remains authoritative for:

```text
accelerometer full-scale range
accelerometer sensitivity
impact detection axis
impact threshold
accelerometer operating mode
```

The Python application must not duplicate those values.

---

# Build Modes

Provide two PlatformIO environments.

Normal firmware:

```ini
[env:tinypico]
platform = espressif32 @ 6.7.0
board = tinypico
framework = espidf

monitor_speed = 115200
```

Calibration firmware:

```ini
[env:tinypico_calibration]
extends = env:tinypico

build_flags =
    ${env:tinypico.build_flags}
    -DCALIBRATION_MODE=1

monitor_speed = 921600
```

Firmware code must support:

```c
#ifndef CALIBRATION_MODE
#define CALIBRATION_MODE 0
#endif
```

Normal builds must continue to use the existing human-readable ESP-IDF logging behavior.

Calibration builds redefine the USB serial interface as a binary data transport.

---

# Calibration Firmware Behavior

When:

```c
CALIBRATION_MODE == 1
```

the firmware must:

1. initialize the LIS2DW12;
2. configure the same measurement mode used by the production impact detector;
3. set LIS2DW12 ODR to 800 Hz;
4. initialize the serial transport at 921600 baud;
5. disable ordinary `ESP_LOGx` output on that serial transport;
6. send a metadata packet;
7. continuously acquire all three accelerometer axes at 800 Hz;
8. continuously stream samples through the binary protocol;
9. maintain sample/drop counters;
10. respond to a host metadata-request packet.

After the binary calibration protocol becomes active, **no human-readable text may be emitted to the serial port**.

Fatal/runtime errors must be represented as binary protocol packets.

---

# Serial Transport

Use:

```text
921600 baud
8 data bits
no parity
1 stop bit
```

Do not use newline-delimited text, CSV, JSON, ESP_LOG output, or printf-style sample records.

The serial stream is machine-readable only.

---

# Packet Framing

Use **COBS framing** with `0x00` as the packet delimiter.

Packet wire format:

```text
COBS_ENCODED_PACKET
0x00
COBS_ENCODED_PACKET
0x00
...
```

The unencoded packet is:

```text
+----------------+----------------+----------------+----------------+
| protocol_ver   | packet_type    | payload_len LE | payload        |
| uint8          | uint8          | uint16         | N bytes        |
+----------------+----------------+----------------+----------------+
| crc32 LE                                                       |
| uint32                                                         |
+----------------------------------------------------------------+
```

CRC32 covers:

```text
protocol_ver
packet_type
payload_len
payload
```

Use standard CRC-32/ISO-HDLC:

```text
polynomial    0x04C11DB7
reflected     yes
init          0xFFFFFFFF
xorout        0xFFFFFFFF
```

Protocol version for this task:

```text
1
```

All multi-byte integer fields are little-endian.

---

# Packet Types

Define:

```c
typedef enum {
    CAL_PKT_METADATA       = 0x01,
    CAL_PKT_SAMPLE_BATCH   = 0x02,
    CAL_PKT_STATUS         = 0x03,
    CAL_PKT_ERROR          = 0x04,

    CAL_CMD_GET_METADATA   = 0x80,
} calibration_packet_type_t;
```

The host sends command packets using the same COBS + CRC framing.

---

# Metadata Packet

`CAL_PKT_METADATA` is the authoritative source for graph setup.

Payload:

```c
typedef struct __attribute__((packed)) {
    uint16_t sample_rate_hz;
    uint8_t  range_g;
    uint8_t  impact_axis;
    int16_t  impact_threshold_raw;
    float    sensitivity_mg_lsb;
    uint8_t  int1_gpio;
    uint8_t  int2_gpio;
    uint8_t  sample_format_version;
    uint8_t  reserved;
} calibration_metadata_v1_t;
```

Define impact-axis values:

```c
typedef enum {
    CAL_AXIS_X = 0,
    CAL_AXIS_Y = 1,
    CAL_AXIS_Z = 2,
} calibration_axis_t;
```

For the current hardware the metadata will contain:

```text
sample_rate_hz    800
int1_gpio         14
int2_gpio         4
```

The following values must come from the actual compiled accelerometer/impact configuration:

```text
range_g
impact_axis
impact_threshold_raw
sensitivity_mg_lsb
```

Do not create separate calibration-only copies of those constants.

---

# Metadata Is the Single Source of Truth

The Python application must use the metadata packet to configure:

```text
graph vertical range
graph axis label
impact threshold lines
raw-to-g conversion
expected sample rate
30-second buffer capacity
```

The application must not contain hard-coded values such as:

```python
RANGE_G = 16
THRESHOLD_RAW = 6000
AXIS = "Z"
SENSITIVITY = ...
SAMPLE_RATE = 800
```

except protocol-level defaults used only for validation/error messages.

When a new metadata packet arrives, the application must immediately update its model and graph configuration.

---

# Graph Range

The graph Y range is defined directly by firmware metadata.

For:

```text
range_g = R
```

render:

```text
-R ... +R g
```

Example only:

```text
range_g = 16

graph range:
-16 g ... +16 g
```

Do not autoscale the graph from observed data.

Do not hard-code ±16 g in the application.

---

# Threshold Lines

The firmware provides:

```text
impact_threshold_raw
sensitivity_mg_lsb
```

The application computes:

```python
threshold_g = (
    abs(impact_threshold_raw)
    * sensitivity_mg_lsb
    / 1000.0
)
```

For absolute-axis impact detection:

```text
abs(axis_sample) >= threshold
```

render two horizontal threshold lines:

```text
+threshold_g
-threshold_g
```

The graph must update these lines whenever new metadata is received.

No threshold constant may be duplicated in Python.

---

# Selected Axis

The firmware provides:

```text
impact_axis
```

The graph displays only that axis.

Mapping:

```text
CAL_AXIS_X -> X
CAL_AXIS_Y -> Y
CAL_AXIS_Z -> Z
```

The application still retains X, Y, and Z samples for the entire 30-second history.

Changing the firmware impact axis and reflashing must change the displayed axis automatically after reconnecting, with no Python code changes.

---

# Sample Acquisition

The firmware must acquire actual sensor data at:

```text
800 samples / second
```

Do not implement acquisition as a free-running polling loop.

Use a deterministic sample-ready mechanism.

Preferred routing:

```text
INT1 / GPIO14 -> existing impact/tap function
INT2 / GPIO4  -> LIS2DW12 data-ready interrupt
```

Configure LIS2DW12 INT2 for data-ready.

The GPIO ISR must not:

```text
perform I2C
format packets
write UART
allocate memory
log
```

The ISR only wakes/notifies the sample task.

Recommended flow:

```text
LIS2DW12 DRDY
      |
      v
GPIO4 ISR
      |
      v
FreeRTOS task notification
      |
      v
sample task
      |
      +--> read X/Y/Z
      |
      +--> timestamp
      |
      +--> assign sequence number
      |
      v
sample queue
      |
      v
serial transport task
```

---

# Sample Structure

Internal firmware sample:

```c
typedef struct {
    uint32_t seq;
    uint32_t timestamp_us;
    int16_t x_raw;
    int16_t y_raw;
    int16_t z_raw;
} calibration_sample_t;
```

Sequence number:

```text
increments once per acquired sample
uint32 wrap is allowed
```

Timestamp:

```text
microseconds
lower uint32 of esp_timer_get_time()
uint32 wrap is allowed
```

---

# Sample Batch Packet

Do not transmit one framed packet per individual sample.

Batch samples to reduce framing and serial-driver overhead.

Use:

```text
16 samples per CAL_PKT_SAMPLE_BATCH packet
```

Payload:

```c
typedef struct __attribute__((packed)) {
    uint8_t count;
    calibration_sample_t samples[16];
} calibration_sample_batch_v1_t;
```

For a normal full packet:

```text
count = 16
```

A partial packet is allowed during shutdown/error handling:

```text
1 <= count <= 16
```

At 800 Hz:

```text
800 / 16 = 50 sample packets per second
```

This packet rate is low enough for the Python and Android parsers while preserving all 800 Hz data.

---

# Throughput Requirement

Each sample is:

```text
4 bytes sequence
4 bytes timestamp
2 bytes X
2 bytes Y
2 bytes Z
----------------
14 bytes
```

Raw sample payload rate:

```text
14 * 800 = 11200 bytes/s
```

Including batching, packet headers, CRC, COBS overhead, and UART framing, the expected transport rate remains comfortably below the capacity of:

```text
921600 baud
```

The implementation must nevertheless verify dropped samples through sequence numbers and firmware counters.

---

# Firmware Queues and Counters

Maintain:

```c
uint32_t samples_acquired;
uint32_t samples_transmitted;
uint32_t samples_dropped;
```

Use a fixed-size FreeRTOS queue between sample acquisition and serial transmission.

No heap allocation is permitted in the per-sample path after initialization.

If the queue is full:

```text
increment samples_dropped
```

and later emit a status/error packet.

---

# Status Packet

Define:

```c
typedef struct __attribute__((packed)) {
    uint32_t samples_acquired;
    uint32_t samples_transmitted;
    uint32_t samples_dropped;
    uint32_t uptime_ms;
} calibration_status_v1_t;
```

Send `CAL_PKT_STATUS` once per second.

This packet allows the application to show:

```text
firmware acquired
firmware transmitted
firmware dropped
uptime
```

---

# Error Packet

Use `CAL_PKT_ERROR`.

Payload:

```c
typedef struct __attribute__((packed)) {
    uint16_t error_code;
    int32_t detail;
} calibration_error_v1_t;
```

Define at minimum:

```c
typedef enum {
    CAL_ERR_ACCEL_INIT   = 1,
    CAL_ERR_I2C_READ     = 2,
    CAL_ERR_SAMPLE_DROP  = 3,
    CAL_ERR_UART_WRITE   = 4,
    CAL_ERR_BAD_COMMAND  = 5,
} calibration_error_code_t;
```

There must be no plain-text error output once calibration transport is active.

---

# Host Metadata Request

The host must be able to request metadata after connecting to firmware that is already running.

Send:

```text
packet_type = CAL_CMD_GET_METADATA
payload_len = 0
```

Firmware replies with:

```text
CAL_PKT_METADATA
```

This removes dependence on catching the original boot-time metadata packet.

---

# Python/Kivy Application

Create:

```text
calibration_app/
```

Suggested structure:

```text
calibration_app/
├── main.py
├── calibration.kv
├── requirements.txt
├── buildozer.spec
├── calibration.spec
├── app/
│   ├── __init__.py
│   ├── cobs.py
│   ├── crc.py
│   ├── protocol.py
│   ├── model.py
│   ├── graph.py
│   └── controller.py
├── serial_backends/
│   ├── __init__.py
│   ├── base.py
│   ├── windows_serial.py
│   └── android_usb_serial.py
└── tests/
    ├── test_cobs.py
    ├── test_crc.py
    ├── test_protocol.py
    ├── test_model.py
    └── test_downsampling.py
```

Use Kivy for the shared application UI.

Do not use Matplotlib for the live graph.

Use a custom Kivy Canvas graph.

---

# Serial Backends

Use a platform-neutral serial interface.

```python
from dataclasses import dataclass
from typing import Protocol


@dataclass(frozen=True)
class SerialDevice:
    id: str
    label: str


class SerialBackend(Protocol):
    def list_devices(self) -> list[SerialDevice]:
        ...

    def connect(self, device_id: str, baudrate: int) -> None:
        ...

    def read(self, size: int) -> bytes:
        ...

    def write(self, data: bytes) -> None:
        ...

    def close(self) -> None:
        ...

    @property
    def connected(self) -> bool:
        ...
```

Implement:

```text
WindowsSerialBackend
AndroidUsbSerialBackend
```

---

# Windows Backend

Use:

```python
serial.tools.list_ports.comports()
```

Populate the dropdown from currently available COM ports.

Example display:

```text
COM5 — USB-Enhanced-SERIAL CH9102
```

Open the selected device using:

```text
921600 baud
8N1
```

Do not assume a fixed COM port.

---

# Android Backend

Use:

```text
Android USB Host API
PyJNIus
usb-serial-for-android
```

Enumerate USB devices using:

```text
UsbManager
UsbSerialProber
```

Open the TinyPICO CH9102F through the supported USB serial driver.

Display USB devices in the same UI dropdown abstraction used on Windows.

Handle:

```text
USB permission request
permission denied
device detached
device reattached
serial read failure
```

without crashing.

Android devices must support USB Host/OTG.

---

# Android Build

Use:

```text
Buildozer
python-for-android
```

Required Python packages:

```text
python3
kivy
pyjnius
```

Include:

```text
com.github.mik3y:usb-serial-for-android:3.11.0
```

as a Gradle dependency.

Declare:

```xml
<uses-feature
    android:name="android.hardware.usb.host"
    android:required="true" />
```

Deliverable:

```text
CaneCalibration.apk
```

---

# Windows Build

Use PyInstaller.

Deliverable:

```text
CaneCalibration.exe
```

or a self-contained Windows distribution.

The packaged application must run without requiring a separately installed Python interpreter.

---

# Binary Reader

Never read the serial port on the Kivy UI thread.

Use:

```text
serial backend
      |
      v
reader thread
      |
      v
byte accumulator
      |
      v
split on 0x00
      |
      v
COBS decode
      |
      v
CRC32 verification
      |
      v
protocol decode
      |
      v
thread-safe event/sample queue
      |
      v
Kivy controller
```

The reader must support arbitrary USB read boundaries.

One packet may arrive across many `read()` calls.

Many packets may arrive in one `read()` call.

---

# Host Protocol Validation

For every packet:

1. COBS decode;
2. validate minimum packet length;
3. validate protocol version;
4. validate payload length;
5. validate CRC32;
6. dispatch by packet type.

Invalid packets must:

```text
increment bad_packet counter
be discarded
not terminate the app
```

---

# Host Sequence Validation

Track the expected sample sequence.

Detect:

```text
missing samples
duplicate samples
out-of-order samples
uint32 wrap
```

Expose a cumulative:

```text
host_sequence_gaps
```

counter to the UI.

Firmware `samples_dropped` and host sequence gaps are separate metrics and must both be displayed.

---

# 30-Second Data Model

The application retains the latest 30 seconds according to firmware metadata.

Do not hard-code:

```text
24000
```

Compute:

```python
capacity = round(metadata.sample_rate_hz * 30)
```

With the current metadata:

```text
800 * 30 = 24000 samples
```

Retain:

```text
sequence
timestamp_us
x_raw
y_raw
z_raw
```

for every sample.

Raw values remain unchanged in the model.

Convert to g only for rendering or presentation.

---

# Live Graph

The graph displays:

```text
last 30 seconds
```

X axis:

```text
-30 seconds ... 0 seconds
```

Y axis:

```text
-metadata.range_g ... +metadata.range_g
```

Selected data series:

```text
metadata.impact_axis
```

Threshold lines:

```text
±(
    abs(metadata.impact_threshold_raw)
    * metadata.sensitivity_mg_lsb
    / 1000
)
```

Everything above must update when a new metadata packet is received.

---

# Graph Rendering Performance

Do not render all retained samples directly.

Use min/max bin downsampling.

For graph width `W` pixels:

1. divide the visible sample window into approximately `W` bins;
2. calculate minimum and maximum selected-axis values in each bin;
3. retain temporal ordering;
4. render at most approximately `2 * W` points.

This preserves short cane-impact spikes.

Target graph refresh rate:

```text
20 Hz
```

Serial acquisition remains independent at:

```text
800 Hz
```

---

# UI

Main screen:

```text
┌─────────────────────────────────────────────┐
│ Device: [ dropdown                       ]  │
│ [Refresh] [Connect / Disconnect]            │
├─────────────────────────────────────────────┤
│ Connected                                   │
│ Rate: <metadata> Hz                         │
│ Range: ±<metadata> g                        │
│ Axis: <metadata axis>                       │
│ Threshold: ±<derived threshold> g           │
│ FW dropped: N   Host gaps: N   Bad packets:N│
├─────────────────────────────────────────────┤
│                                             │
│                 LIVE GRAPH                  │
│                                             │
│ +range ───────────────────────────────────  │
│                                             │
│ +threshold - - - - - - - - - - - - - -   │
│                                             │
│                  signal                     │
│                                             │
│ -threshold - - - - - - - - - - - - - -   │
│                                             │
│ -range ───────────────────────────────────  │
│                                             │
│ -30 s                               0 s     │
└─────────────────────────────────────────────┘
```

Support portrait and landscape layouts.

---

# Connection Workflow

## Windows

```text
launch
→ Refresh
→ select COM port
→ Connect
→ open at 921600
→ start binary reader
→ send CAL_CMD_GET_METADATA
→ receive CAL_PKT_METADATA
→ configure model and graph
→ process CAL_PKT_SAMPLE_BATCH
```

## Android

```text
launch
→ Refresh
→ enumerate USB devices
→ select TinyPICO
→ Connect
→ obtain USB permission
→ open at 921600
→ start binary reader
→ send CAL_CMD_GET_METADATA
→ receive CAL_PKT_METADATA
→ configure model and graph
→ process CAL_PKT_SAMPLE_BATCH
```

---

# Disconnect Behavior

On disconnect:

1. stop the reader thread;
2. close the serial backend;
3. preserve the most recent graph;
4. show disconnected state;
5. allow immediate reconnect.

Android USB detach must invoke the same behavior automatically.

---

# Firmware Tests

Both builds must succeed:

```bash
pio run -e tinypico
pio run -e tinypico_calibration
```

Calibration build must verify:

```text
921600 serial configuration
800 Hz LIS2DW12 configuration
INT1 GPIO14
INT2 GPIO4
binary calibration transport enabled
ordinary ESP_LOG serial output disabled after protocol startup
```

---

# Protocol Unit Tests

Implement tests for:

1. COBS encode/decode round trip;
2. payload containing zero bytes;
3. CRC32 known vector;
4. metadata packet encode/decode;
5. sample batch packet encode/decode;
6. status packet encode/decode;
7. error packet encode/decode;
8. invalid CRC rejection;
9. invalid payload length rejection;
10. unsupported protocol version rejection;
11. packet split across many serial reads;
12. many packets in one serial read;
13. sequence-gap detection;
14. sequence uint32 wrap;
15. timestamp uint32 wrap;
16. malformed packet recovery followed by successful parsing of the next packet.

---

# Metadata-Driven Graph Tests

Create test metadata:

```text
range_g = 4
impact_axis = X
impact_threshold_raw = 1024
sensitivity_mg_lsb = 0.976
sample_rate_hz = 400
```

Expected:

```text
graph Y range = -4 .. +4 g
display axis = X
history capacity = 12000 samples
threshold lines derived from 1024 * 0.976 / 1000
```

Then inject different metadata:

```text
range_g = 16
impact_axis = Z
impact_threshold_raw = 4096
sensitivity_mg_lsb = 1.952
sample_rate_hz = 800
```

Expected:

```text
graph reconfigures automatically
history capacity becomes 24000
axis becomes Z
threshold lines move
no Python constants are edited
```

---

# Downsampling Test

Generate synthetic 30-second data containing:

```text
low-amplitude baseline
one single-sample positive impact
one single-sample negative impact
```

Downsample to a graph width much smaller than the sample count.

Expected:

```text
both spikes remain represented
```

Reject simple every-Nth-sample decimation.

---

# Hardware Acceptance Test: Sustained Stream

Run calibration firmware and application continuously for:

```text
10 minutes
```

At 800 Hz, expected acquired sample count is approximately:

```text
480000
```

Acceptance:

```text
firmware samples_dropped == 0
host sequence gaps == 0
CRC/bad packet count == 0 under normal USB conditions
application remains responsive
graph continues updating
```

---

# Hardware Acceptance Test: Metadata Authority

Test at least two firmware configurations.

Configuration A:

```text
one full-scale range
one impact axis
one threshold
```

Configuration B:

```text
different full-scale range
different impact axis
different threshold
```

Rebuild and flash between configurations.

The Python application must automatically update:

```text
Y-axis range
axis label
threshold lines
sample capacity if sample rate changes
```

No Python source modification is allowed.

---

# Hardware Acceptance Test: Windows

On Windows:

1. connect TinyPICO;
2. press Refresh;
3. select the CH9102F COM port;
4. connect;
5. verify metadata;
6. verify 800 Hz stream;
7. verify 30-second graph;
8. run for 10 minutes.

The application must remain responsive.

---

# Hardware Acceptance Test: Android

On a USB Host/OTG-capable Android device:

1. connect TinyPICO through USB OTG;
2. press Refresh;
3. select the TinyPICO USB serial device;
4. accept the USB permission prompt;
5. connect;
6. verify metadata;
7. verify 800 Hz stream;
8. verify 30-second graph;
9. run for 10 minutes.

The application must remain responsive.

---

# Remove Previous Calibration Implementation

Remove or do not implement:

```text
firmware accelerometer ring buffer
firmware CSV dump
capture dump serial command
human-readable calibration sample output
CSV parser in host application
hard-coded Python graph range
hard-coded Python impact threshold
hard-coded Python selected axis
```

The host owns the rolling 30-second history.

The firmware owns:

```text
sensor acquisition
authoritative metadata
sample sequencing
binary transport
drop counters
protocol errors
```

---

# Definition of Done

The task is complete when:

- normal firmware still builds and operates normally;
- calibration firmware builds with `CALIBRATION_MODE=1`;
- LIS2DW12 streams actual samples at 800 Hz;
- LIS2DW12 INT1 is on GPIO14;
- LIS2DW12 INT2 is on GPIO4;
- calibration serial runs at 921600 baud;
- calibration serial output is binary machine-readable only;
- no ordinary log text corrupts the calibration stream;
- packets use COBS framing and CRC32;
- samples are transmitted in batches of 16;
- every sample contains sequence, timestamp, X, Y, and Z;
- firmware metadata is authoritative for sample rate, range, sensitivity, impact axis, and threshold;
- Python contains no duplicated graph-range or impact-threshold constants;
- the graph range updates from firmware metadata;
- the graph selected axis updates from firmware metadata;
- threshold lines update from firmware metadata;
- the host retains the latest 30 seconds based on metadata sample rate;
- min/max graph downsampling preserves short impacts;
- Windows device enumeration and connection work;
- Android USB Host enumeration, permission, and connection work;
- a 10-minute 800 Hz hardware test completes with zero firmware sample drops and zero host sequence gaps;
- Windows packaging produces a runnable application;
- Android packaging produces a runnable APK.
