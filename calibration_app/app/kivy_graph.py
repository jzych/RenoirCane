from kivy.graphics import Color, Line
from kivy.uix.widget import Widget

from app.graph import min_max_downsample


class CalibrationGraph(Widget):
    def render(self, config, samples) -> None:
        self.canvas.clear()
        if config is None or not samples or self.width <= 0 or self.height <= 0:
            return
        selected = {"X": "x_raw", "Y": "y_raw", "Z": "z_raw"}[config.axis_label]
        points = [(sample.timestamp_us, getattr(sample, selected) * config.sensitivity_mg_lsb / 1000.0) for sample in samples]
        downsampled = min_max_downsample(points, int(self.width))
        start, end = downsampled[0][0], downsampled[-1][0]
        span = max(end - start, 1)
        def xy(point):
            return ((point[0] - start) * self.width / span + self.x, (point[1] + config.range_g) * self.height / (2 * config.range_g) + self.y)
        with self.canvas:
            Color(0.35, 0.75, 1.0, 1.0)
            Line(points=[coordinate for point in downsampled for coordinate in xy(point)], width=1.2)
            Color(1.0, 0.55, 0.1, 1.0)
            for threshold in (config.threshold_g, -config.threshold_g):
                y = (threshold + config.range_g) * self.height / (2 * config.range_g) + self.y
                Line(points=[self.x, y, self.right, y], width=1.0)
