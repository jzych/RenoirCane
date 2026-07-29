# Calibration UI Snapshot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render TinyPICO calibration samples from a consistent snapshot so concurrent serial input cannot terminate the Android application.

**Architecture:** `CalibrationModel` gains an internal lock and a `graph_snapshot()` method that copies its configuration, samples, and gap count atomically. The Kivy app obtains that snapshot at 20 Hz and passes the copied sequence into `CalibrationGraph.render()`; rendering occurs after the lock is released.

**Tech Stack:** Python 3, threading, pytest, Kivy, Buildozer, Android USB host API.

## Global Constraints

- The graph must not iterate `CalibrationModel.samples` directly.
- Metadata application, sample append, and snapshot creation share one model lock.
- A sample that arrives during a UI copy may appear on the next 20 Hz refresh.
- Keep serial reader, packet parsing, firmware-status, and USB behavior unchanged.
- Verify on Samsung SM-A037G with the connected TinyPICO.

---

### Task 1: Add a consistent graph-history snapshot

**Files:**

- Modify: `calibration_app/tests/test_model.py`
- Modify: `calibration_app/app/model.py:26-45`

**Interfaces:**

- Produces: `CalibrationModel.graph_snapshot() -> tuple[GraphConfig | None, tuple[Sample, ...], int]`.
- Consumes: metadata and sample writes through the existing `apply_metadata()` and `append_samples()` methods.

- [ ] **Step 1: Write the failing model snapshot test**

Add this test using the existing `metadata()` helper:

```python
def test_graph_snapshot_copies_config_samples_and_gap_count():
    model = CalibrationModel()
    model.apply_metadata(metadata(1, 16, 1, 6000, 0.976))
    model.append_samples([Sample(1, 10, 1, 2, 3), Sample(3, 20, 4, 5, 6)])

    config, samples, gaps = model.graph_snapshot()
    model.append_samples([Sample(4, 30, 7, 8, 9)])

    assert config.axis_label == "Y"
    assert [sample.sequence for sample in samples] == [1, 3]
    assert gaps == 1
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
wsl.exe -d Ubuntu bash -lc 'cd /mnt/m/CodingProjects/RenoirCane/calibration_app && /tmp/renoircane-calibration-tests/bin/python -m pytest tests/test_model.py::test_graph_snapshot_copies_config_samples_and_gap_count -q'
```

Expected: FAIL because `CalibrationModel` does not provide `graph_snapshot()`.

- [ ] **Step 3: Implement locked model updates and snapshot**

Add `self._lock = threading.Lock()` in `CalibrationModel.__init__()`. Wrap the state mutations in `apply_metadata()` and `append_samples()` with `with self._lock:`. Add:

```python
def graph_snapshot(self) -> tuple[GraphConfig | None, tuple[Sample, ...], int]:
    with self._lock:
        return self.graph_config, tuple(self.samples), self.host_sequence_gaps
```

- [ ] **Step 4: Run the focused model test and full suite**

Run:

```powershell
wsl.exe -d Ubuntu bash -lc 'cd /mnt/m/CodingProjects/RenoirCane/calibration_app && /tmp/renoircane-calibration-tests/bin/python -m pytest tests/test_model.py::test_graph_snapshot_copies_config_samples_and_gap_count -q && /tmp/renoircane-calibration-tests/bin/python -m pytest tests -q'
```

Expected: both commands pass.

### Task 2: Render immutable snapshot samples

**Files:**

- Modify: `calibration_app/tests/test_main.py`
- Modify: `calibration_app/main.py:81-88`
- Modify: `calibration_app/app/kivy_graph.py:8-29`

**Interfaces:**

- Consumes: `CalibrationModel.graph_snapshot() -> tuple[GraphConfig | None, tuple[Sample, ...], int]`.
- Produces: `CalibrationGraph.render(config: GraphConfig, samples: tuple[Sample, ...]) -> None`.

- [ ] **Step 1: Write the failing UI-refresh test**

Add `SimpleNamespace`, `GraphConfig`, and `Sample` imports. Add a recorder and
this test to `CalibrationAppTests`:

```python
class GraphRecorder:
    def render(self, config, samples):
        self.received = config, samples


def test_refresh_renders_the_model_snapshot_not_live_samples(self):
    config = GraphConfig("Y", 4, 12000, 1.0, 0.976)
    samples = (Sample(1, 10, 1, 2, 3),)
    root = CalibrationRoot()
    root.reader = SimpleNamespace(error=None)
    root.controller.model = SimpleNamespace(
        graph_snapshot=lambda: (config, samples, 7)
    )
    root.controller.firmware_status = SimpleNamespace(samples_dropped=2)
    recorder = GraphRecorder()
    root.ids["graph"] = recorder

    root.refresh(0)

    self.assertEqual(recorder.received, (config, samples))
    self.assertTrue(root.status_text.endswith("gaps 7"))
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
wsl.exe -d Ubuntu bash -lc 'cd /mnt/m/CodingProjects/RenoirCane/calibration_app && /tmp/renoircane-calibration-tests/bin/python -m pytest tests/test_main.py::CalibrationAppTests::test_refresh_renders_the_model_snapshot_not_live_samples -q'
```

Expected: FAIL because `refresh()` still reads the live model fields and `render()` accepts the model object.

- [ ] **Step 3: Implement snapshot-based refresh and rendering**

In `CalibrationRoot.refresh()`, call `config, samples, host_sequence_gaps = self.controller.model.graph_snapshot()`. When `config` exists, format the current status with `host_sequence_gaps` and call `self.ids.graph.render(config, samples)`.

Change `CalibrationGraph.render()` to accept `config` and `samples`. Keep its empty/size checks, and construct graph points by iterating only `samples`.

- [ ] **Step 4: Run the focused UI test and full suite**

Run the new named UI test, then all tests through the WSL pytest environment.

Expected: all pass.

- [ ] **Step 5: Rebuild, install, and test on the phone**

Build the local debug APK with Buildozer, install it through the connected wireless ADB target, and launch it. With the TinyPICO connected, press Refresh, select the discovered USB serial device, accept permission if requested, and press Connect.

Expected: metadata arrives, live Y-axis samples render, and logcat contains neither `deque mutated during iteration` nor another Python traceback.

- [ ] **Step 6: Commit implementation and plan**

```powershell
git add calibration_app/app/model.py calibration_app/app/kivy_graph.py calibration_app/main.py calibration_app/tests/test_model.py calibration_app/tests/test_main.py docs/superpowers/plans/2026-07-28-calibration-ui-snapshot.md
git commit -m "fix: render calibration samples from snapshots"
```
