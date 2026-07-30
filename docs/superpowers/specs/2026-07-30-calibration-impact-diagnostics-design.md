# Calibration Impact Diagnostics Design

## Goal

Make calibration-mode impact decisions observable on both Windows and Android, so a missing graph marker can be traced to the exact firmware gate that rejected it.

## Scope

The firmware will publish an additive `CAL_PKT_IMPACT_TELEMETRY` packet once per status interval. Its packed payload contains seven unsigned 32-bit counters:

1. `int1_received`
2. `tap_confirmed`
3. `threshold_rejected`
4. `refractory_skipped`
5. `audio_busy_skipped`
6. `event_packets_sent`
7. `event_queue_dropped`

The desktop and Android Kivy clients share the same Python controller and status text, so both will decode and display the same telemetry without platform-specific UI changes.

## Data flow

The INT1 ISR increments `int1_received`. The impact task increments `tap_confirmed` only after `accelerometer_read_impact_status()` reports a single tap. It classifies the sample with the existing impact classifier: below-threshold candidates increment `threshold_rejected`, refractory candidates increment `refractory_skipped`, and otherwise-audio-blocked candidates increment `audio_busy_skipped`. Every successful UART write of an impact event increments `event_packets_sent`; a full impact-event queue increments `event_queue_dropped` rather than blocking classification indefinitely.

The new packet is emitted independently of the existing status packet. This keeps the current status payload backward compatible and makes unsupported diagnostic telemetry safely ignorable by old clients.

## Testing

Firmware unit tests will prove the packed telemetry packet round trips and that exact software-threshold and signed-saturation inputs classify as processed. Existing below-threshold, refractory, and audio-busy tests will remain the evidence for the remaining branches.

Python tests will prove that the controller decodes telemetry and the shared Kivy root status includes all six counters. The host model/sample tests will include an over-threshold sample fixture to ensure the displayed data stream preserves it; this does not assert that a graph crossing creates an impact event, because the sensor interrupt remains a separate prerequisite.

## Manual acceptance

With calibration firmware flashed, a tap should visibly advance `INT1` and normally `tap`. If a marker is absent, the displayed counters identify whether the device did not recognize a tap, rejected the fresh sample threshold, rejected it as refractory/audio-busy, or failed to send the event packet.
