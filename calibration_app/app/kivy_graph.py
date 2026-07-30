from kivy.graphics import Color, Line
from kivy.uix.widget import Widget

from app.graph import impact_event_offsets_us, min_max_downsample, timestamp_offset_us


class CalibrationGraph(Widget):
    def render(self, config, samples, impact_events) -> None:
        self.canvas.clear()
        if config is None or not samples or self.width <= 0 or self.height <= 0:
            return
        selected = {"X": "x_raw", "Y": "y_raw", "Z": "z_raw"}[config.axis_label]
        start_timestamp_us = samples[0].timestamp_us
        points = [
            (
                timestamp_offset_us(sample.timestamp_us, start_timestamp_us),
                getattr(sample, selected) * config.sensitivity_mg_lsb / 1000.0,
            )
            for sample in samples
        ]
        downsampled = min_max_downsample(points, int(self.width))
        sample_span_us = timestamp_offset_us(samples[-1].timestamp_us, start_timestamp_us)
        span = max(sample_span_us, 1)
        display_range_g = config.display_range_g

        def xy(point):
            return (point[0] * self.width / span + self.x, (point[1] + display_range_g) * self.height / (2 * display_range_g) + self.y)

        visible_events = tuple(
            event
            for event in impact_events
            if timestamp_offset_us(event.timestamp_us, start_timestamp_us) <= sample_span_us
        )
        visible_event_offsets = impact_event_offsets_us(
            tuple(event.timestamp_us for event in impact_events),
            start_timestamp_us=start_timestamp_us,
            end_timestamp_us=samples[-1].timestamp_us,
        )
        with self.canvas:
            Color(0.35, 0.75, 1.0, 1.0)
            Line(points=[coordinate for point in downsampled for coordinate in xy(point)], width=1.2)
            Color(1.0, 0.55, 0.1, 1.0)
            for threshold in (config.threshold_g, -config.threshold_g):
                y = (threshold + display_range_g) * self.height / (2 * display_range_g) + self.y
                Line(points=[self.x, y, self.right, y], width=1.0)
            for event, event_offset_us in zip(visible_events, visible_event_offsets):
                Color(*(0.25, 0.9, 0.35, 1.0) if event.outcome == "processed" else (1.0, 0.85, 0.1, 1.0))
                x = event_offset_us * self.width / span + self.x
                Line(points=[x, self.y, x, self.top], width=1.4)
