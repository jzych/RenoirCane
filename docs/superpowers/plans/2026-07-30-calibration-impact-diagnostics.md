# Calibration Impact Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose each calibration impact-detection gate to the Windows and Android calibration clients and cover threshold/saturation behavior deterministically.

**Architecture:** Add an independent fixed-size firmware telemetry packet so the existing status payload stays compatible. Update the shared Python protocol/controller/status text to display that packet. Keep classification in `impact.c`; telemetry is recorded at its true ISR, task, and transport boundaries.

**Tech Stack:** ESP-IDF/FreeRTOS C, PlatformIO/Unity, Python, Kivy, pytest.

## Global Constraints

- Calibration transport remains COBS-framed UART at 921600 baud.
- The telemetry packet has seven little-endian `uint32_t` counters and is emitted once per existing status interval; `event_queue_dropped` makes transport saturation visible.
- Both Windows and Android use the shared Python controller/status presentation.
- Tests must be written and observed failing before production code is added.

---

### Task 1: Define the telemetry protocol and firmware counters

**Files:**
- Modify: `components/calibration/include/calibration_protocol.h`
- Modify: `components/calibration/calibration_stream.c`
- Test: `test/test_calibration_protocol/test_calibration_protocol.c`

**Interfaces:**
- Produces: `CAL_PKT_IMPACT_TELEMETRY` and `calibration_impact_telemetry_v1_t` with counters in this order: `int1_received`, `tap_confirmed`, `threshold_rejected`, `refractory_skipped`, `audio_busy_skipped`, `event_packets_sent`, `event_queue_dropped`.

- [x] **Step 1: Write failing protocol and classifier tests**

```c
void test_impact_telemetry_packet_round_trip_preserves_all_counters(void) {
    calibration_impact_telemetry_v1_t expected = {
        .int1_received = 1, .tap_confirmed = 2, .threshold_rejected = 3,
        .refractory_skipped = 4, .audio_busy_skipped = 5, .event_packets_sent = 6, .event_queue_dropped = 7,
    };
    /* Encode/decode CAL_PKT_IMPACT_TELEMETRY and compare sizeof(expected) bytes. */
}

void test_impact_classifier_accepts_exact_threshold_and_signed_saturation(void) {
    /* With a 6000 Y threshold, assert 6000, -6000, INT16_MAX and INT16_MIN classify as processed. */
}
```

- [x] **Step 2: Run the selected PlatformIO test image and confirm it fails because the packet type and struct are undefined**

Run: `calibration_app\\.venv\\Scripts\\pio.exe test -e tinypico_test -f test_calibration_protocol --without-uploading --without-testing`

- [x] **Step 3: Add the packet contract and counters**

```c
typedef struct __attribute__((packed)) {
    uint32_t int1_received, tap_confirmed, threshold_rejected;
    uint32_t refractory_skipped, audio_busy_skipped, event_packets_sent;
} calibration_impact_telemetry_v1_t;
```

Increment `int1_received` in the impact ISR, classification counters in `impact_task`, and `event_packets_sent` only after `write_packet(CAL_PKT_IMPACT_EVENT, ...)` succeeds. A full impact-event queue increments `event_queue_dropped` with a non-blocking send. Snapshot the counters safely before sending the telemetry packet every second.

- [x] **Step 4: Re-run the selected PlatformIO test image and confirm it builds successfully**

- [x] **Step 5: Commit firmware protocol/counter work**

```bash
git add components/calibration test/test_calibration_protocol
git commit -m "feat: report calibration impact diagnostics"
```

### Task 2: Decode and surface telemetry in the shared app

**Files:**
- Modify: `calibration_app/app/protocol.py`
- Modify: `calibration_app/app/controller.py`
- Modify: `calibration_app/main.py`
- Test: `calibration_app/tests/test_controller.py`
- Test: `calibration_app/tests/test_main.py`

**Interfaces:**
- Consumes: `<IIIIIII` telemetry payload from `CAL_PKT_IMPACT_TELEMETRY`.
- Produces: immutable `ImpactTelemetry` attached to `CalibrationController`; the status text reports `irq`, `tap`, `threshold`, `refractory`, `busy`, `sent`, and `queue`.

- [x] **Step 1: Write failing controller and root-status tests**

```python
def test_controller_records_impact_telemetry():
    controller.handle_packet(Packet(1, CAL_PKT_IMPACT_TELEMETRY, struct.pack("<IIIIIII", 1, 2, 3, 4, 5, 6, 7)))
    assert controller.impact_telemetry.event_packets_sent == 6

def test_refresh_shows_impact_telemetry(root):
    root.controller.impact_telemetry = ImpactTelemetry(1, 2, 3, 4, 5, 6)
    root.refresh(0)
    assert "irq 1" in root.status_text and "sent 6" in root.status_text
```

- [x] **Step 2: Run those tests and confirm they fail because the protocol constant and telemetry state do not exist**

Run: `python -m pytest tests/test_controller.py tests/test_main.py -q`

- [x] **Step 3: Implement protocol decoding and status display**

```python
@dataclass(frozen=True)
class ImpactTelemetry:
    int1_received: int = 0
    tap_confirmed: int = 0
    threshold_rejected: int = 0
    refractory_skipped: int = 0
    audio_busy_skipped: int = 0
    event_packets_sent: int = 0
```

Reset telemetry in `reset_session()`, reject malformed payloads through the existing bad-payload path, and append the six compact values to the existing connected status string.

- [x] **Step 4: Run the controller/root tests and the full non-Kivy host suite**

Run: `python -m pytest tests --ignore=tests/test_main.py -q -p no:cacheprovider --basetemp .pytest-tmp`

- [x] **Step 5: Commit shared-client telemetry work**

```bash
git add calibration_app
git commit -m "feat: show calibration impact diagnostics"
```

### Task 3: Verify the firmware images and integration contract

**Files:**
- Modify: `calibration_app/tests/test_model.py`

- [x] **Step 1: Write a failing model fixture proving an over-threshold raw sample is retained in the graph snapshot**

```python
def test_graph_snapshot_preserves_samples_beyond_the_marked_threshold():
    # Metadata threshold=6000, then append a Y sample of 6001.
    assert snapshot_samples[-1].y_raw == 6001
```

- [x] **Step 2: Run it and confirm the expected coverage fixture is missing**

- [x] **Step 3: Add the minimal test fixture; production behavior already preserves raw samples**

- [x] **Step 4: Build both firmware environments and run all available host tests**

Run: `calibration_app\\.venv\\Scripts\\pio.exe run -e tinypico`

Run: `calibration_app\\.venv\\Scripts\\pio.exe run -e tinypico_calibration`

- [x] **Step 5: Commit the final tests and documentation**

```bash
git add docs/superpowers calibration_app/tests/test_model.py
git commit -m "test: cover calibration impact diagnostics"
```
