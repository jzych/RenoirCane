import importlib
import sys
from types import ModuleType

from app.model import GraphConfig, ImpactEvent, Sample


def test_impact_markers_use_outcome_specific_opacity_and_draw_before_the_acceleration_trace(monkeypatch):
    instructions = []

    class FakeCanvas:
        def clear(self):
            instructions.clear()

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            return False

    class FakeWidget:
        def __init__(self):
            self.canvas = FakeCanvas()
            self.x = 0
            self.y = 0
            self.width = 100
            self.height = 100
            self.right = 100
            self.top = 100

    class FakeColor:
        def __init__(self, *rgba):
            instructions.append(("color", rgba))

    class FakeLine:
        def __init__(self, **kwargs):
            instructions.append(("line", kwargs))

    kivy = ModuleType("kivy")
    graphics = ModuleType("kivy.graphics")
    uix = ModuleType("kivy.uix")
    widget = ModuleType("kivy.uix.widget")
    graphics.Color = FakeColor
    graphics.Line = FakeLine
    widget.Widget = FakeWidget
    monkeypatch.setitem(sys.modules, "kivy", kivy)
    monkeypatch.setitem(sys.modules, "kivy.graphics", graphics)
    monkeypatch.setitem(sys.modules, "kivy.uix", uix)
    monkeypatch.setitem(sys.modules, "kivy.uix.widget", widget)
    monkeypatch.delitem(sys.modules, "app.kivy_graph", raising=False)

    graph_module = importlib.import_module("app.kivy_graph")
    graph = graph_module.CalibrationGraph()
    graph.render(
        GraphConfig("Y", 16, 24_000, 2.928, 0.488),
        (Sample(1, 100, 0, 6000, 0),),
        (ImpactEvent(100, "processed"), ImpactEvent(100, "skipped")),
    )

    marker_color = ("color", (0.25, 0.9, 0.35, 1.0))
    skipped_marker_color = ("color", (1.0, 0.85, 0.1, 0.35))
    marker_line = ("line", {"points": [0.0, 0, 0.0, 100], "width": 1.4})
    trace_color = ("color", (0.35, 0.75, 1.0, 1.0))
    assert marker_color in instructions
    assert skipped_marker_color in instructions
    assert marker_line in instructions
    assert trace_color in instructions
    assert instructions.index(marker_color) < instructions.index(skipped_marker_color) < instructions.index(trace_color)
