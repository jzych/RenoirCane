# Calibration UI Snapshot Design

## Goal

Prevent the Android calibration application from crashing when its reader
thread receives samples while the Kivy UI renders the graph.

## Cause

`CalibrationReader` calls `CalibrationController.handle_packet()` on a
background thread. Sample handling appends to `CalibrationModel.samples`, a
mutable `deque`. At 20 Hz the Kivy UI iterates that deque directly in
`CalibrationGraph.render()`. A concurrent append raises `RuntimeError: deque
mutated during iteration`, terminating the application after metadata and
samples are received.

## Design

`CalibrationModel` will own a lock for all graph configuration and sample
history updates. It will expose one snapshot operation that copies the current
graph configuration, sample history, and host sequence-gap count while holding
that lock. The returned sample collection is immutable from the UI's
perspective.

Metadata application and sample appends will use the same lock. The Kivy
refresh path will obtain one model snapshot, use its configuration and gap
count for status text, and pass its copied samples to the graph renderer. The
renderer will no longer iterate `CalibrationModel.samples`.

The existing controller API and background-reader architecture remain in
place. Firmware status remains an immutable replacement value and is not part
of the graph-history lock.

## Timing and Error Handling

The reader holds the lock only while updating in-memory state. The UI holds it
only while copying the history; graph downsampling and canvas drawing occur
after release. A sample arriving during snapshot creation appears on the next
20 Hz frame. Serial reads, metadata parsing, and existing disconnection
handling are unchanged.

## Tests and Verification

Tests will prove that a snapshot preserves a coherent configuration and sample
history and that a renderer can consume the snapshot collection without access
to the mutable deque. The full calibration-app test suite will run. A local
Android APK will then be built, installed on the Samsung SM-A037G, and checked
with the TinyPICO attached: after Connect, metadata and samples must render
without a Python traceback or application termination.
