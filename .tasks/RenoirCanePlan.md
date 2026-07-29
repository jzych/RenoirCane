# Cane Impact Audio ECU Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build ESP-IDF firmware for a TinyPICO v3 installed inside a cane that detects cane-to-ground impacts using an LIS2DW12 accelerometer and immediately plays an embedded WAV-derived PCM sound through a paired external Bluetooth speaker using Bluetooth Classic A2DP.

**Architecture:** The ESP32 runs ESP-IDF with the Bluedroid Bluetooth Classic stack and operates as an A2DP Source. The LIS2DW12 is connected over I2C and raises an INT1 interrupt for candidate impacts; a FreeRTOS task validates each event, applies a refractory interval, and triggers audio playback. Debug/status data is emitted over the TinyPICO USB serial connection using ESP-IDF logging, with an optional raw accelerometer capture mode for impact tuning.

**Tech Stack:** TinyPICO v3 / ESP32, PlatformIO espressif32 6.7.0, ESP-IDF 5.2.1, Bluedroid, A2DP Source, FreeRTOS, ESP-IDF I2C master driver, ST `lis2dw12_reg.c/.h`, ESP-IDF GPIO, ADC calibration, NVS, ESP_LOG, embedded PCM audio.

## Global Constraints

- PlatformIO platform: `espressif32 @ 6.7.0`.
- ESP-IDF version: 5.2.1.
- Framework: ESP-IDF.
- Bluetooth transport: Bluetooth Classic using Bluedroid.
- Audio role: A2DP Source connected to an external Bluetooth speaker acting as A2DP Sink.
- Speaker connection remains established while the cane is in active operating mode.
- Audio asset is stored in firmware flash as embedded PCM data generated from a WAV source at build time.
- A2DP audio input uses ESP-IDF 5.2.1 `esp_a2d_source_register_data_callback()` with raw PCM supplied by the application.
- PCM format: 16-bit signed little-endian, 44.1 kHz, stereo.
- Accelerometer: LIS2DW12 / DFRobot SEN0405 over I2C.
- LIS2DW12 I2C address: `0x19`.
- I2C SDA: GPIO21.
- I2C SCL: GPIO22.
- LIS2DW12 INT1 default GPIO: GPIO27.
- Initial LIS2DW12 measurement range: ±16 g.
- Initial LIS2DW12 output data rate: 800 Hz.
- Candidate impact source: LIS2DW12 single-tap interrupt routed to INT1.
- Candidate impact validation runs in a FreeRTOS task, never in the GPIO ISR.
- Initial impact refractory interval: 200 ms.
- Onboard APA102 status LED data: GPIO2.
- Onboard APA102 status LED clock: GPIO12.
- Onboard APA102 power control: GPIO13, active-low power enable.
- TinyPICO battery charge-status input: GPIO34.
- TinyPICO battery-voltage ADC input: GPIO35 / ADC1 channel 7.
- TinyPICO battery divider: upper resistor value `442`, lower resistor value `160`; multiply calibrated ADC millivolts by `(442 + 160) / 160 = 3.7625`.
- TinyPICO charge-status input on GPIO34 is active-low.
- Logging transport: USB serial/UART via ESP-IDF `ESP_LOGx`.
- Normal monitor baud rate: 115200.
- Raw accelerometer capture must not log each sample through `ESP_LOGx`; use buffered CSV output.
- Use NVS namespace `speaker`, key `bda`, to store the selected Bluetooth speaker address.
- First-time speaker provisioning is performed through serial commands; normal boots connect directly to the saved BD_ADDR.
- Do not call Bluetooth, audio, I2C, logging, or allocation APIs from the accelerometer GPIO ISR.
- Active operating mode keeps the ESP32 awake and the A2DP link available for low trigger-to-sound latency.

---

## Target Repository Structure

```text
.
├── CMakeLists.txt
├── platformio.ini
├── partitions.csv
├── sdkconfig.defaults
├── tools/
│   └── wav_to_pcm.py
├── assets/
│   ├── impact.wav
│   └── impact.pcm
├── components/
│   ├── app_config/
│   │   ├── CMakeLists.txt
│   │   ├── include/app_config.h
│   │   └── Kconfig
│   ├── status_led/
│   │   ├── CMakeLists.txt
│   │   ├── include/status_led.h
│   │   └── status_led.c
│   ├── battery/
│   │   ├── CMakeLists.txt
│   │   ├── include/battery.h
│   │   └── battery.c
│   ├── accelerometer/
│   │   ├── CMakeLists.txt
│   │   ├── include/accelerometer.h
│   │   ├── accelerometer.c
│   │   ├── lis2dw12_reg.c
│   │   └── include/lis2dw12_reg.h
│   ├── impact/
│   │   ├── CMakeLists.txt
│   │   ├── include/impact.h
│   │   └── impact.c
│   ├── accel_capture/
│   │   ├── CMakeLists.txt
│   │   ├── include/accel_capture.h
│   │   └── accel_capture.c
│   ├── audio/
│   │   ├── CMakeLists.txt
│   │   ├── include/audio.h
│   │   └── audio.c
│   └── bluetooth/
│       ├── CMakeLists.txt
│       ├── include/bluetooth_audio.h
│       └── bluetooth_audio.c
├── main/
│   ├── CMakeLists.txt
│   └── main.c
└── test/
    ├── test_impact_logic/
    │   └── test_impact_logic.c
    └── test_audio_asset/
        └── test_audio_asset.py
```

## Public Component Interfaces

### `components/status_led/include/status_led.h`

```c
#pragma once

#include <stdint.h>

typedef enum {
    STATUS_LED_OFF = 0,
    STATUS_LED_BOOT,
    STATUS_LED_BT_CONNECTING,
    STATUS_LED_READY,
    STATUS_LED_PLAYING,
    STATUS_LED_ERROR,
} status_led_state_t;

void status_led_init(void);
void status_led_set(status_led_state_t state);
void status_led_power_off(void);
```

### `components/battery/include/battery.h`

```c
#pragma once

#include <stdbool.h>

void battery_init(void);
float battery_get_voltage(void);
bool battery_is_charging(void);
```

### `components/accelerometer/include/accelerometer.h`

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
    int64_t timestamp_us;
} accel_sample_t;

typedef void (*accelerometer_irq_callback_t)(void);

void accelerometer_init(accelerometer_irq_callback_t irq_callback);
bool accelerometer_read_sample(accel_sample_t *sample);
bool accelerometer_read_impact_status(void);
void accelerometer_clear_interrupt(void);
```

### `components/impact/include/impact.h`

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "accelerometer.h"

typedef struct {
    uint32_t refractory_ms;
    int16_t validation_threshold_raw;
} impact_config_t;

typedef struct {
    int64_t timestamp_us;
    accel_sample_t sample;
    uint32_t peak_abs_raw;
} impact_event_t;

void impact_init(const impact_config_t *config);
bool impact_validate_candidate(const accel_sample_t *sample, int64_t now_us, impact_event_t *event);
void impact_mark_accepted(int64_t timestamp_us);
```

### `components/accel_capture/include/accel_capture.h`

```c
#pragma once

#include <stddef.h>
#include "accelerometer.h"

void accel_capture_init(size_t capacity_samples);
void accel_capture_push(const accel_sample_t *sample);
void accel_capture_on_impact(void);
void accel_capture_dump_csv(void);
```

### `components/audio/include/audio.h`

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void audio_init(void);
bool audio_trigger(void);
void audio_stop(void);
bool audio_is_playing(void);
size_t audio_fill_pcm(uint8_t *dst, size_t requested_bytes);
```

### `components/bluetooth/include/bluetooth_audio.h`

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BT_AUDIO_DISABLED = 0,
    BT_AUDIO_CONNECTING,
    BT_AUDIO_CONNECTED,
    BT_AUDIO_STREAMING,
    BT_AUDIO_ERROR,
} bt_audio_state_t;

typedef void (*bt_audio_state_callback_t)(bt_audio_state_t state);

void bluetooth_audio_init(bt_audio_state_callback_t state_callback);
void bluetooth_audio_start(void);
void bluetooth_audio_start_discovery(void);
bool bluetooth_audio_connect_and_save(const uint8_t bda[6]);
bool bluetooth_audio_connect_saved(void);
bool bluetooth_audio_is_connected(void);
bt_audio_state_t bluetooth_audio_get_state(void);
```

---

### Task 1: Establish ESP-IDF PlatformIO Project Configuration

**Files:**
- Create/modify: `platformio.ini`
- Create/modify: `sdkconfig.defaults`
- Create/modify: `CMakeLists.txt`
- Create/modify: `main/CMakeLists.txt`
- Create: `partitions.csv`
- Create: `components/app_config/CMakeLists.txt`
- Create: `components/app_config/include/app_config.h`
- Create: `components/app_config/Kconfig`

**Interfaces:**
- Consumes: none.
- Produces: build configuration and centralized hardware/application constants used by every later component.

- [ ] **Step 1: Configure PlatformIO**

Use:

```ini
[env:tinypico]
platform = espressif32 @ 6.7.0
board = tinypico
framework = espidf

monitor_speed = 115200
monitor_filters =
    esp32_exception_decoder

board_build.partitions = partitions.csv

build_flags =
    -Wall
    -Wextra
    -Werror=return-type
```

- [ ] **Step 2: Configure Bluetooth Classic and logging defaults**

Create `sdkconfig.defaults` containing:

```text
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_A2DP_ENABLE=y
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200
CONFIG_NVS_ENCRYPTION=n
```

- [ ] **Step 3: Create partition table**

Create `partitions.csv`:

```csv
# Name,   Type, SubType, Offset,   Size, Flags
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  0x3F0000,
```

- [ ] **Step 4: Centralize hardware constants**

Create `components/app_config/include/app_config.h`:

```c
#pragma once

#include "driver/gpio.h"

#define APP_I2C_PORT              0
#define APP_I2C_SDA_GPIO          GPIO_NUM_21
#define APP_I2C_SCL_GPIO          GPIO_NUM_22
#define APP_I2C_FREQ_HZ           400000

#define APP_ACCEL_I2C_ADDR        0x19
#define APP_ACCEL_INT_GPIO        GPIO_NUM_27
#define APP_ACCEL_ODR_HZ          800
#define APP_ACCEL_RANGE_G         16

#define APP_IMPACT_REFRACTORY_MS  200
#define APP_IMPACT_THRESHOLD_RAW  6000

#define APP_TAP_THS_X             0x08
#define APP_TAP_THS_Y             0x08
#define APP_TAP_THS_Z             0x08
#define APP_TAP_SHOCK             0x02
#define APP_TAP_QUIET             0x01
#define APP_TAP_LATENCY           0x00

#define APP_LED_DATA_GPIO         GPIO_NUM_2
#define APP_LED_CLOCK_GPIO        GPIO_NUM_12
#define APP_LED_POWER_GPIO        GPIO_NUM_13

#define APP_BAT_CHARGE_GPIO       GPIO_NUM_34
#define APP_BAT_VOLTAGE_GPIO      GPIO_NUM_35

#define APP_AUDIO_SAMPLE_RATE     44100
#define APP_AUDIO_CHANNELS        2
#define APP_AUDIO_BITS_PER_SAMPLE 16

#define APP_ACCEL_CAPTURE_MS_PRE  500
#define APP_ACCEL_CAPTURE_MS_POST 500
```

- [ ] **Step 5: Build empty application**

Run:

```bash
pio run
```

Expected: successful ESP-IDF firmware build for `tinypico`.

- [ ] **Step 6: Commit**

```bash
git add platformio.ini sdkconfig.defaults partitions.csv CMakeLists.txt main components/app_config
git commit -m "build: configure TinyPICO ESP-IDF project"
```

---

### Task 2: Implement Native TinyPICO APA102 Status LED

**Files:**
- Create: `components/status_led/CMakeLists.txt`
- Create: `components/status_led/include/status_led.h`
- Create: `components/status_led/status_led.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/main.c`

**Interfaces:**
- Consumes: GPIO constants from `app_config.h`.
- Produces: `status_led_init()`, `status_led_set()`, `status_led_power_off()`.

- [ ] **Step 1: Implement GPIO initialization**

Configure GPIO2, GPIO12, and GPIO13 as outputs. Set GPIO13 high before enabling LED data/clock activity so the LED starts physically powered off.

- [ ] **Step 2: Implement APA102 byte writer**

Implement:

```c
static void apa102_write_byte(uint8_t value)
{
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(APP_LED_DATA_GPIO, (value >> bit) & 0x01);
        gpio_set_level(APP_LED_CLOCK_GPIO, 1);
        gpio_set_level(APP_LED_CLOCK_GPIO, 0);
    }
}
```

- [ ] **Step 3: Implement single-pixel RGB output**

Write:
- four `0x00` start bytes,
- one LED frame byte `0xE4`,
- B, G, R bytes,
- four `0xFF` end bytes.

Use low LED intensity for all status states.

- [ ] **Step 4: Map status states**

Use the following state behavior:

```text
OFF            physical LED power off
BOOT           dim white
BT_CONNECTING  dim blue
READY          dim green
PLAYING        dim cyan
ERROR          dim red
```

- [ ] **Step 5: Hardware verification**

Flash:

```bash
pio run -t upload
pio device monitor
```

Call each state sequentially from `app_main()` with a 1-second delay.

Expected:
- all states visibly distinct,
- `STATUS_LED_OFF` completely powers the onboard APA102 down.

- [ ] **Step 6: Remove the temporary LED cycling code**

Leave `app_main()` calling only:

```c
status_led_init();
status_led_set(STATUS_LED_BOOT);
```

- [ ] **Step 7: Commit**

```bash
git add components/status_led main
git commit -m "feat: add TinyPICO status LED driver"
```

---

### Task 3: Add Battery Measurement and Charge Status

**Files:**
- Create: `components/battery/CMakeLists.txt`
- Create: `components/battery/include/battery.h`
- Create: `components/battery/battery.c`
- Modify: `main/main.c`

**Interfaces:**
- Consumes: GPIO34 and GPIO35 constants from `app_config.h`.
- Produces: `battery_init()`, `battery_get_voltage()`, `battery_is_charging()`.

- [ ] **Step 1: Initialize battery ADC**

Use ESP-IDF ADC oneshot on ADC1 channel 7 (GPIO35), 12-bit width, `ADC_ATTEN_DB_11`, and initialize the ESP32 line-fitting ADC calibration scheme.

- [ ] **Step 2: Convert ADC result to battery voltage**

Read 16 ADC samples and average them. Convert the averaged raw value to millivolts through the ADC calibration API, then compute battery millivolts as `adc_mv * 602 / 160`. Return volts as `battery_mv / 1000.0f`. Define `BAT_DIVIDER_UPPER = 442` and `BAT_DIVIDER_LOWER = 160` in `battery.c`.

- [ ] **Step 3: Initialize charge status input**

Configure GPIO34 as an input. Return `true` only when GPIO34 reads low:

```c
bool battery_is_charging(void);
```

- [ ] **Step 4: Add periodic diagnostic output**

In the initial application loop, every 10 seconds log:

```text
I (...) BATTERY: voltage=3.91V charging=0
```

- [ ] **Step 5: Hardware verification**

Run from LiPo power and USB power.

Expected:
- battery voltage is stable and physically plausible,
- charging state is asserted while the TinyPICO charger reports active charging and clears when charging stops.

- [ ] **Step 6: Commit**

```bash
git add components/battery main
git commit -m "feat: add TinyPICO battery monitoring"
```

---

### Task 4: Integrate LIS2DW12 I2C Driver

**Files:**
- Create: `components/accelerometer/CMakeLists.txt`
- Add: `components/accelerometer/lis2dw12_reg.c`
- Add: `components/accelerometer/include/lis2dw12_reg.h`
- Create: `components/accelerometer/include/accelerometer.h`
- Create: `components/accelerometer/accelerometer.c`
- Modify: `main/main.c`

**Interfaces:**
- Consumes: I2C and accelerometer constants from `app_config.h`.
- Produces: the complete `accelerometer.h` interface.

- [ ] **Step 1: Vendor the ST LIS2DW12 register driver**

Add the official ST `lis2dw12_reg.c` and `lis2dw12_reg.h` unchanged.

- [ ] **Step 2: Initialize ESP-IDF I2C master bus**

Configure:
- controller: I2C0,
- SDA: GPIO21,
- SCL: GPIO22,
- frequency: 400 kHz,
- device address: `0x19`.

- [ ] **Step 3: Implement ST read/write callbacks**

Provide callbacks matching `stmdev_ctx_t` that call the ESP-IDF I2C master transmit/receive APIs.

Return a negative result from the callbacks on transport failure.

- [ ] **Step 4: Verify LIS2DW12 identity**

Read `WHO_AM_I` during `accelerometer_init()`.

Fail initialization and log an error when the returned identity does not match the LIS2DW12 device ID.

- [ ] **Step 5: Configure LIS2DW12 sampling**

Configure:
- ±16 g full scale,
- 800 Hz ODR,
- XYZ axes enabled,
- Block Data Update enabled.

- [ ] **Step 6: Implement sample reads**

`accelerometer_read_sample()` must:
- read X/Y/Z raw samples,
- store raw signed 16-bit values,
- store `esp_timer_get_time()` as `timestamp_us`,
- return `false` on I2C failure.

- [ ] **Step 7: Log live samples at 10 Hz for bring-up**

Temporarily log:

```text
I (...) ACCEL: x=... y=... z=...
```

Expected:
- stationary values are stable,
- rotating the board changes the dominant gravity axis,
- no recurring I2C errors.

- [ ] **Step 8: Remove temporary sample logging**

Leave sample access available only through the component API.

- [ ] **Step 9: Commit**

```bash
git add components/accelerometer main
git commit -m "feat: add LIS2DW12 accelerometer driver"
```

---

### Task 5: Configure LIS2DW12 Single-Tap Interrupt

**Files:**
- Modify: `components/accelerometer/accelerometer.c`
- Modify: `components/accelerometer/include/accelerometer.h`

**Interfaces:**
- Consumes: `accelerometer_init(accelerometer_irq_callback_t)`.
- Produces: candidate-impact interrupt notification and status clearing.

- [ ] **Step 1: Configure LIS2DW12 tap engine**

Configure single-tap detection on X, Y, and Z axes and route the tap event to INT1.

Program the initial tap values from `app_config.h`: X/Y/Z threshold `0x08`, SHOCK `0x02`, QUIET `0x01`, LATENCY `0x00`. Enable single-tap recognition on X, Y, and Z and route the single-tap event to INT1.

- [ ] **Step 2: Configure ESP32 interrupt input**

Configure GPIO27 as an input using the LIS2DW12 INT1 polarity selected in the register configuration.

Install the ESP-IDF GPIO ISR service.

- [ ] **Step 3: Keep ISR minimal**

Implement an IRAM-safe ISR that only invokes the callback supplied to `accelerometer_init()`.

The callback used by the application must only notify a task using `vTaskNotifyGiveFromISR()`.

- [ ] **Step 4: Implement interrupt status read/clear**

`accelerometer_read_impact_status()` must query the LIS2DW12 event source register.

`accelerometer_clear_interrupt()` must perform the required source-register read/clear sequence.

- [ ] **Step 5: Hardware verification**

Log one message from task context per tap candidate:

```text
I (...) ACCEL: impact candidate
```

Strike the cane repeatedly against a solid floor.

Expected:
- candidate events appear,
- no ISR watchdog or cache errors,
- no log calls execute inside the ISR.

- [ ] **Step 6: Commit**

```bash
git add components/accelerometer
git commit -m "feat: add LIS2DW12 impact interrupt"
```

---

### Task 6: Implement Impact Validation and Refractory Logic

**Files:**
- Create: `components/impact/CMakeLists.txt`
- Create: `components/impact/include/impact.h`
- Create: `components/impact/impact.c`
- Create: `test/test_impact_logic/test_impact_logic.c`
- Modify: `main/main.c`

**Interfaces:**
- Consumes: `accel_sample_t`.
- Produces: `impact_validate_candidate()` and `impact_mark_accepted()`.

- [ ] **Step 1: Write tests for peak calculation**

Test samples with positive and negative values:

```c
TEST_CASE("impact peak uses maximum absolute axis", "[impact]")
{
    impact_config_t cfg = {
        .refractory_ms = 200,
        .validation_threshold_raw = 6000,
    };

    impact_init(&cfg);

    accel_sample_t sample = {
        .x = -400,
        .y = 7200,
        .z = -6800,
        .timestamp_us = 1000000,
    };

    impact_event_t event = {0};

    TEST_ASSERT_TRUE(
        impact_validate_candidate(&sample, 1000000, &event)
    );
    TEST_ASSERT_EQUAL_UINT32(7200, event.peak_abs_raw);
}
```

- [ ] **Step 2: Write threshold rejection test**

Use a sample whose maximum absolute axis is below `validation_threshold_raw`.

Expected: `impact_validate_candidate()` returns `false`.

- [ ] **Step 3: Write refractory rejection test**

Accept an event at `1,000,000 us`, call `impact_mark_accepted()`, then validate a second threshold-exceeding event at `1,100,000 us`.

Expected: rejected because it is inside 200 ms.

Validate another event at `1,250,000 us`.

Expected: accepted.

- [ ] **Step 4: Implement minimal impact logic**

Compute:

```c
peak = max(abs(x), abs(y), abs(z));
```

Reject when:
- `peak < validation_threshold_raw`,
- elapsed time since the last accepted event is less than the configured refractory interval.

Populate `impact_event_t` for accepted candidates.

- [ ] **Step 5: Run tests**

Run the project's ESP-IDF/PlatformIO test target for `test_impact_logic`.

Expected: all impact logic tests pass.

- [ ] **Step 6: Integrate an impact task**

Initialize impact logic with `.refractory_ms = APP_IMPACT_REFRACTORY_MS` and `.validation_threshold_raw = APP_IMPACT_THRESHOLD_RAW`.

Create one application task that waits on a direct task notification.

On notification:
1. read and clear LIS2DW12 impact status,
2. read one accelerometer sample,
3. call `impact_validate_candidate()`,
4. log accepted events,
5. call `impact_mark_accepted()`.

Do not trigger audio yet.

- [ ] **Step 7: Commit**

```bash
git add components/impact test/test_impact_logic main
git commit -m "feat: validate cane impact events"
```

---

### Task 7: Add Raw Accelerometer Ring Capture and CSV Dump

**Files:**
- Create: `components/accel_capture/CMakeLists.txt`
- Create: `components/accel_capture/include/accel_capture.h`
- Create: `components/accel_capture/accel_capture.c`
- Modify: `main/main.c`

**Interfaces:**
- Consumes: `accel_sample_t`.
- Produces: rolling pre-impact samples plus post-impact samples downloadable over USB serial.

- [ ] **Step 1: Allocate the capture ring**

At 800 Hz, reserve enough samples for:
- 500 ms before impact,
- 500 ms after impact.

Minimum capacity:

```text
800 samples/second × 1 second = 800 samples
```

Allocate the capture ring from internal 8-bit-capable RAM with `heap_caps_calloc(..., MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)`. If allocation returns `NULL`, disable capture, log `ACCEL_CAPTURE: allocation failed`, and continue normal impact/audio operation.

- [ ] **Step 2: Add a sampling task**

Create a FreeRTOS task using `vTaskDelayUntil()` or an `esp_timer` wake mechanism to read accelerometer samples at the configured capture cadence and push each sample into the ring.

- [ ] **Step 3: Freeze a capture around an accepted impact**

When `accel_capture_on_impact()` is called:
- mark the current ring index as the trigger position,
- continue collecting the configured 500 ms post-trigger window,
- freeze the completed capture.

- [ ] **Step 4: Implement CSV dump**

`accel_capture_dump_csv()` writes directly to stdout/UART:

```text
timestamp_us,raw_x,raw_y,raw_z
12345678,-120,34,16120
...
```

Use buffered writes; do not call `ESP_LOGI()` for each sample.

- [ ] **Step 5: Trigger CSV dump through a serial command**

Add a simple console command:

```text
capture dump
```

Expected: the most recent frozen capture is emitted as CSV.

- [ ] **Step 6: Hardware verification**

Collect multiple strikes on solid ground.

Expected:
- approximately 500 ms of pre-impact history,
- the impact appears close to the trigger boundary,
- approximately 500 ms of post-impact history,
- no serial logging from ISR context.

- [ ] **Step 7: Commit**

```bash
git add components/accel_capture main
git commit -m "feat: capture accelerometer impact traces"
```

---

### Task 8: Add Deterministic WAV-to-PCM Asset Conversion

**Files:**
- Create: `tools/wav_to_pcm.py`
- Add: `assets/impact.wav`
- Generate: `assets/impact.pcm`
- Create: `test/test_audio_asset/test_audio_asset.py`
- Modify: `components/audio/CMakeLists.txt`

**Interfaces:**
- Consumes: `assets/impact.wav`.
- Produces: raw 16-bit 44.1 kHz stereo PCM binary embedded into the firmware image.

- [ ] **Step 1: Write asset format test**

Use Python's standard `wave` module to assert that `assets/impact.wav` has:

```python
assert wav.getframerate() == 44100
assert wav.getnchannels() == 2
assert wav.getsampwidth() == 2
assert wav.getcomptype() == "NONE"
```

- [ ] **Step 2: Implement converter**

`tools/wav_to_pcm.py` must:
- open WAV through `wave`,
- reject any format not matching the required PCM format,
- write only frame bytes to the destination `.pcm`,
- print sample count and resulting byte size.

Usage:

```bash
python tools/wav_to_pcm.py assets/impact.wav assets/impact.pcm
```

- [ ] **Step 3: Generate PCM asset**

Run:

```bash
python tools/wav_to_pcm.py assets/impact.wav assets/impact.pcm
```

- [ ] **Step 4: Embed the PCM file**

In `components/audio/CMakeLists.txt`, use ESP-IDF binary embedding so the linker exports start/end symbols for `assets/impact.pcm`.

- [ ] **Step 5: Run asset tests**

Run:

```bash
python -m unittest test/test_audio_asset/test_audio_asset.py
```

Expected: pass.

- [ ] **Step 6: Commit**

```bash
git add tools assets test/test_audio_asset components/audio/CMakeLists.txt
git commit -m "feat: embed impact PCM audio asset"
```

---

### Task 9: Implement PCM Playback State

**Files:**
- Create: `components/audio/include/audio.h`
- Create: `components/audio/audio.c`
- Modify: `components/audio/CMakeLists.txt`

**Interfaces:**
- Consumes: linker-embedded `impact.pcm`.
- Produces: `audio_trigger()`, `audio_stop()`, `audio_is_playing()`, `audio_fill_pcm()`.

- [ ] **Step 1: Implement embedded asset boundaries**

Declare linker symbols for the beginning and end of the embedded PCM file.

Compute immutable asset length during `audio_init()`.

- [ ] **Step 2: Implement playback cursor**

Maintain:
- `playing`,
- current byte offset,
- asset length.

Protect the playback cursor and `playing` flag with a static `portMUX_TYPE` initialized using `portMUX_INITIALIZER_UNLOCKED` and `portENTER_CRITICAL()` / `portEXIT_CRITICAL()`.

- [ ] **Step 3: Implement trigger**

`audio_trigger()`:
- resets the cursor to byte 0,
- sets playback active,
- returns `true`.

Calling it for a new accepted impact restarts the sound from the beginning.

- [ ] **Step 4: Implement PCM fill**

`audio_fill_pcm(dst, requested_bytes)`:
- copies available PCM bytes into `dst`,
- zero-fills any remaining requested bytes after the asset ends,
- clears `playing` once the asset reaches its end,
- returns the number of meaningful PCM bytes copied before zero-fill.

- [ ] **Step 5: Build**

Run:

```bash
pio run
```

Expected: embedded asset symbols resolve and firmware links successfully.

- [ ] **Step 6: Commit**

```bash
git add components/audio
git commit -m "feat: add embedded PCM playback state"
```

---

### Task 10: Implement Bluetooth Classic A2DP Source and Speaker Provisioning

**Files:**
- Create: `components/bluetooth/CMakeLists.txt`
- Create: `components/bluetooth/include/bluetooth_audio.h`
- Create: `components/bluetooth/bluetooth_audio.c`
- Modify: `main/main.c`

**Interfaces:**
- Consumes: `audio_fill_pcm()`.
- Produces: persistent speaker discovery/selection, saved direct reconnect, A2DP connection state.

- [ ] **Step 1: Initialize NVS and Bluetooth controller**

Initialize:
1. NVS,
2. Bluetooth controller in Classic BT mode,
3. Bluedroid using `esp_bluedroid_init_with_cfg()` with `esp_bluedroid_config_t.ssp_en = true`,
4. GAP callbacks,
5. A2DP Source callbacks.

Set the local device name to:

```text
Cane Audio ECU
```

- [ ] **Step 2: Initialize A2DP Source**

Register the A2DP source PCM callback with `esp_a2d_source_register_data_callback()`. In that callback, pass the requested buffer and length to:

```c
audio_fill_pcm(...)
```

The embedded asset and callback contract are 44.1 kHz, 16-bit, stereo PCM.

- [ ] **Step 3: Implement state tracking**

Map A2DP/GAP events into:

```c
BT_AUDIO_DISABLED
BT_AUDIO_CONNECTING
BT_AUDIO_CONNECTED
BT_AUDIO_STREAMING
BT_AUDIO_ERROR
```

Invoke the registered application state callback whenever the state changes.

- [ ] **Step 4: Implement first-time discovery**

`bluetooth_audio_start_discovery()`:
- starts Classic Bluetooth inquiry,
- logs discovered device name and BD_ADDR,
- logs every discovered device as `BT_DISCOVERY: <BD_ADDR> <RSSI> <name>`,
- does not persist or auto-connect a discovered address.

- [ ] **Step 5: Implement explicit provisioning and saved direct reconnect**

`bluetooth_audio_connect_and_save(const uint8_t bda[6])`:
- calls `esp_a2d_source_connect()` for the supplied BD_ADDR,
- waits for the A2DP connection event in the Bluetooth state machine,
- writes the 6-byte address to NVS namespace `speaker`, key `bda` only after the connection succeeds.

`bluetooth_audio_connect_saved()`:
- loads `speaker/bda` from NVS,
- directly calls `esp_a2d_source_connect()` for that address,
- returns `false` when no saved address exists or NVS read fails.

- [ ] **Step 6: Implement media start/stop integration**

When `audio_trigger()` transitions into playing and A2DP is connected:
- call `esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START)` when the media channel is not already started.

When the asset finishes:
- call `esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND)` after the final PCM data is drained,
- retain the A2DP ACL/profile connection.

- [ ] **Step 7: Add Bluetooth status LED behavior**

Map:

```text
CONNECTING -> STATUS_LED_BT_CONNECTING
CONNECTED  -> STATUS_LED_READY
STREAMING  -> STATUS_LED_PLAYING
ERROR      -> STATUS_LED_ERROR
```

- [ ] **Step 8: Hardware verification with the target speaker**

First boot:
1. run `bt discover`,
2. identify the target speaker BD_ADDR from the serial output,
3. run `bt connect XX:XX:XX:XX:XX:XX`,
4. verify the address is stored after A2DP connection succeeds.

Second boot:
1. load saved address,
2. connect directly without inquiry.

Expected serial output:

```text
I (...) BT: connecting to saved speaker XX:XX:XX:XX:XX:XX
I (...) BT: A2DP connected
```

- [ ] **Step 9: Commit**

```bash
git add components/bluetooth main
git commit -m "feat: add Bluetooth A2DP source"
```

---

### Task 11: Verify Manual Sound Playback Through A2DP

**Files:**
- Modify: `main/main.c`

**Interfaces:**
- Consumes: `bluetooth_audio_is_connected()`, `audio_trigger()`.
- Produces: validated end-to-end audio path independent of impact detection.

- [ ] **Step 1: Add temporary serial command**

Add:

```text
audio play
```

When invoked:
- verify A2DP is connected,
- call `audio_trigger()`.

- [ ] **Step 2: Verify playback**

Run:

```bash
pio run -t upload
pio device monitor
```

Connect the speaker and issue:

```text
audio play
```

Expected:
- impact sound starts promptly,
- full sound plays once,
- speaker remains connected after playback,
- status LED returns to READY.

- [ ] **Step 3: Repeat playback**

Issue `audio play` at least 20 times.

Expected:
- no connection churn,
- no memory-growth trend,
- no crash,
- identical playback each time.

- [ ] **Step 4: Commit**

```bash
git add main
git commit -m "test: verify manual A2DP impact playback"
```

---

### Task 12: Connect Accepted Impacts to Audio Playback

**Files:**
- Modify: `main/main.c`
- Modify: `components/impact/impact.c` only if integration exposes a logic defect.

**Interfaces:**
- Consumes: accelerometer candidate notifications, impact validator, capture, A2DP state, audio trigger.
- Produces: complete impact-to-sound behavior.

- [ ] **Step 1: Finalize impact task flow**

The task must execute this sequence:

```text
task notification
→ verify LIS2DW12 impact source
→ clear LIS2DW12 interrupt
→ read accel sample
→ validate threshold + refractory interval
→ freeze/start capture post-trigger window
→ log accepted impact
→ audio_trigger()
→ impact_mark_accepted()
```

- [ ] **Step 2: Guard against disconnected speaker**

When an impact is accepted but A2DP is disconnected:
- log the impact,
- do not block waiting for Bluetooth,
- let the Bluetooth state machine reconnect independently.

- [ ] **Step 3: Log accepted event**

Use one structured log line:

```text
I (...) IMPACT: t_us=12345678 peak_raw=12345 x=-123 y=456 z=12000 bt=connected
```

- [ ] **Step 4: Verify one sound per physical strike**

Perform at least 50 deliberate cane strikes on solid ground.

Expected:
- one playback per accepted physical strike,
- no repeated sound from post-impact ringing inside the 200 ms refractory interval.

- [ ] **Step 5: Commit**

```bash
git add main components/impact
git commit -m "feat: trigger A2DP sound on cane impact"
```

---

### Task 13: Add Runtime Diagnostics Commands

**Files:**
- Create: `components/debug_console/CMakeLists.txt`
- Create: `components/debug_console/include/debug_console.h`
- Create: `components/debug_console/debug_console.c`
- Modify: `main/main.c`

**Interfaces:**
- Consumes: battery, accelerometer, capture, audio, Bluetooth component APIs.
- Produces: serial diagnostic commands.

- [ ] **Step 1: Implement command parser**

Support newline-terminated commands from stdin/UART.

Commands:

```text
help
status
audio play
capture dump
bt discover
bt connect XX:XX:XX:XX:XX:XX
bt reconnect
```

- [ ] **Step 2: Implement `status`**

Output:

```text
battery_voltage=<float>
battery_charging=<0|1>
bt_state=<state>
audio_playing=<0|1>
accel_address=0x19
accel_odr_hz=800
accel_range_g=16
impact_refractory_ms=200
```

- [ ] **Step 3: Implement Bluetooth commands**

`bt discover` starts Classic Bluetooth discovery and logs discovered names, RSSI values, and addresses.

`bt connect XX:XX:XX:XX:XX:XX` parses the supplied address and calls `bluetooth_audio_connect_and_save()`.

`bt reconnect` calls `bluetooth_audio_connect_saved()`.

- [ ] **Step 4: Verify all commands over PlatformIO monitor**

Expected: each command returns cleanly without blocking accelerometer interrupt handling or Bluetooth callbacks.

- [ ] **Step 5: Commit**

```bash
git add components/debug_console main
git commit -m "feat: add serial diagnostics console"
```

---

### Task 14: Tune Impact Detection from Captured Cane Data

**Files:**
- Modify: `components/accelerometer/accelerometer.c`
- Modify: `components/impact/impact.c`
- Modify: `components/app_config/include/app_config.h`
- Add: `docs/impact-calibration.md`

**Interfaces:**
- Consumes: raw CSV captures from actual cane use.
- Produces: final LIS2DW12 tap register configuration and software validation threshold.

- [ ] **Step 1: Capture representative data**

Collect traces for:
- at least 30 deliberate strikes on hard flooring,
- at least 30 normal cane swings without ground contact,
- at least 20 gentle cane placements,
- at least 20 side bumps or handling events.

Save each capture with an outcome label.

- [ ] **Step 2: Determine raw software threshold**

For each capture compute:

```text
peak_abs_raw = max(abs(x), abs(y), abs(z))
```

Sort the labeled captures by `peak_abs_raw`. Set `validation_threshold_raw` to the midpoint between the highest observed non-impact peak and the lowest observed deliberate hard-floor impact peak when those distributions do not overlap. If they overlap, keep `APP_IMPACT_THRESHOLD_RAW` unchanged and tune the LIS2DW12 tap thresholds first, then recapture the full dataset and repeat this calculation.

- [ ] **Step 3: Tune LIS2DW12 single-tap registers**

Adjust the named tap threshold and timing constants until the sensor generates candidate interrupts for the desired impacts while minimizing candidates from non-impact motion.

Keep software validation enabled.

- [ ] **Step 4: Record final calibration**

Create `docs/impact-calibration.md` with:
- final tap threshold register values,
- final tap timing register values,
- final software `validation_threshold_raw`,
- final refractory interval,
- test surface categories,
- number of accepted/rejected events in the calibration dataset.

- [ ] **Step 5: Run final detection test**

Perform 100 representative cane cycles.

Record:
- true impacts,
- detected impacts,
- false triggers,
- missed impacts.

- [ ] **Step 6: Commit**

```bash
git add components/accelerometer components/impact components/app_config docs/impact-calibration.md
git commit -m "calibrate: tune cane impact detection"
```

---

### Task 15: Finalize Main Application State Machine

**Files:**
- Modify: `main/main.c`

**Interfaces:**
- Consumes: all component APIs.
- Produces: deterministic boot, reconnect, ready, impact, playback, and error behavior.

- [ ] **Step 1: Implement boot sequence**

Use this order:

```text
NVS
→ status LED
→ battery
→ audio
→ I2C/LIS2DW12
→ impact logic
→ accel capture
→ Bluetooth
→ attempt saved-speaker reconnect
→ debug console
→ worker tasks
```

- [ ] **Step 2: Implement application state**

Use:

```c
typedef enum {
    SYS_BOOT = 0,
    SYS_BT_CONNECTING,
    SYS_READY,
    SYS_PLAYING,
    SYS_ERROR,
} system_state_t;
```

- [ ] **Step 3: Map system state to LED**

```text
SYS_BOOT          -> BOOT
SYS_BT_CONNECTING -> BT_CONNECTING
SYS_READY         -> READY
SYS_PLAYING       -> PLAYING
SYS_ERROR         -> ERROR
```

- [ ] **Step 4: Keep application idle event-driven**

After initialization:
- impact handling blocks on task notifications,
- Bluetooth behavior is callback/state-machine driven,
- diagnostics block on console input,
- housekeeping runs periodically.

Do not add a busy polling loop in `app_main()`.

- [ ] **Step 5: Add housekeeping task**

Every 10 seconds:
- read battery voltage,
- read charging state,
- verify Bluetooth state,
- emit one concise status log.

- [ ] **Step 6: Commit**

```bash
git add main
git commit -m "feat: finalize cane ECU application state machine"
```

---

### Task 16: System Verification

**Files:**
- Create: `docs/system-verification.md`

**Interfaces:**
- Consumes: completed firmware and actual hardware.
- Produces: verification record suitable for regression testing.

- [ ] **Step 1: Clean build**

Run:

```bash
pio run -t clean
pio run
```

Expected: success with no unresolved symbols or fatal compiler warnings.

- [ ] **Step 2: Flash and boot**

Run:

```bash
pio run -t upload
pio device monitor
```

Expected boot sequence:
- TinyPICO starts,
- LIS2DW12 identity is verified,
- saved Bluetooth speaker connection starts,
- A2DP reaches connected state,
- status LED enters READY.

- [ ] **Step 3: Verify impact-to-audio path**

Perform 50 consecutive deliberate impacts.

Expected:
- accepted impact logged,
- one sound playback begins for each accepted impact,
- speaker remains A2DP-connected across repeated triggers,
- no reset, panic, watchdog, or heap failure.

- [ ] **Step 4: Verify refractory behavior**

Create one strike with pronounced mechanical ringing.

Expected:
- only one accepted impact within the configured 200 ms window.

- [ ] **Step 5: Verify serial debug path**

During normal operation execute:

```text
status
audio play
capture dump
```

Expected:
- all commands work while Bluetooth remains connected,
- impact handling continues operating.

- [ ] **Step 6: Verify battery diagnostics**

Run from the 3.7 V LiPo.

Expected:
- plausible calibrated battery voltage,
- charge state observable while USB is connected,
- firmware remains functional when USB monitor is disconnected.

- [ ] **Step 7: Verify direct reconnect**

Power-cycle the cane ECU with the speaker powered on.

Expected:
- firmware loads the saved BD_ADDR,
- connects directly to the known A2DP sink,
- reaches READY without discovery.

- [ ] **Step 8: Record results**

Create `docs/system-verification.md` containing:
- firmware commit SHA,
- speaker make/model,
- speaker BD_ADDR redacted to last two bytes in documentation,
- impact calibration values,
- impact test counts,
- false-trigger count,
- missed-impact count,
- connection/reconnection result,
- battery voltage reading,
- final pass/fail status.

- [ ] **Step 9: Commit**

```bash
git add docs/system-verification.md
git commit -m "test: document system verification"
```

---

## Definition of Done

The project is complete when all of the following are true:

- `pio run` succeeds from a clean checkout.
- TinyPICO initializes LIS2DW12 at address `0x19` on GPIO21/GPIO22.
- LIS2DW12 INT1 produces candidate impact notifications on GPIO27.
- Impact validation runs in task context and enforces the configured threshold and 200 ms refractory interval.
- The selected Bluetooth speaker address is stored in NVS.
- Subsequent boots directly reconnect to the saved Bluetooth speaker.
- ESP32 operates as an A2DP Source using Bluedroid.
- The embedded 16-bit/44.1-kHz/stereo PCM asset plays through the external speaker.
- An accepted cane impact triggers one playback of the embedded sound.
- Repeated mechanical ringing from one strike does not trigger duplicate playback inside the refractory interval.
- A 1-second accelerometer trace containing 500 ms before and 500 ms after the impact can be dumped over USB serial as CSV.
- Battery voltage and charging state are available through the serial `status` command.
- The onboard APA102 reports boot, Bluetooth connection, ready, playback, and error states.
- Normal runtime logs remain readable at 115200 baud.
- No Bluetooth, logging, I2C, audio, or allocation operations execute from the accelerometer GPIO ISR.
- System verification is recorded in `docs/system-verification.md`.
