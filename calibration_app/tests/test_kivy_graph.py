import importlib
import sys
from types import ModuleType

import pytest

from app.model import GraphConfig, ImpactEvent, Sample


@pytest.fixture
def graph_and_instructions(monkeypatch):
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
    return graph_module.CalibrationGraph(), instructions


TRACE_COLOR = ("color", (0.35, 0.75, 1.0, 1.0))
THRESHOLD_COLOR = ("color", (1.0, 0.55, 0.1, 1.0))


def test_impact_markers_use_outcome_specific_opacity_and_draw_before_the_acceleration_trace(graph_and_instructions):
    graph, instructions = graph_and_instructions
    graph.render(
        GraphConfig("Y", 16, 24_000, 2.928, 0.488),
        (Sample(1, 100, 0, 6000, 0),),
        (ImpactEvent(100, "processed"), ImpactEvent(100, "skipped")),
    )

    marker_color = ("color", (0.25, 0.9, 0.35, 1.0))
    skipped_marker_color = ("color", (1.0, 0.85, 0.1, 0.35))
    marker_line = ("line", {"points": [0.0, 0, 0.0, 100], "width": 1.4})
    assert marker_color in instructions
    assert skipped_marker_color in instructions
    assert marker_line in instructions
    assert TRACE_COLOR in instructions
    assert instructions.index(marker_color) < instructions.index(skipped_marker_color) < instructions.index(TRACE_COLOR)


def test_trace_plots_the_configured_axis_and_threshold_lines_at_plus_minus_threshold(graph_and_instructions):
    graph, instructions = graph_and_instructions
    samples = (Sample(1, 0, 1000, 6000, -1000), Sample(2, 1000, 1000, -6000, -1000))

    graph.render(GraphConfig("Y", 16, 24_000, 2.928, 0.488), samples, ())

    # ±16 g range with 20 % headroom -> ±19.2 g over 100 px; 6000 raw * 0.488 mg = 2.928 g.
    high_y = (2.928 + 19.2) * 100 / 38.4
    low_y = (-2.928 + 19.2) * 100 / 38.4
    trace = instructions[instructions.index(TRACE_COLOR) + 1]
    assert trace[0] == "line"
    assert trace[1]["points"] == pytest.approx([0.0, high_y, 100.0, low_y])
    thresholds = instructions[instructions.index(THRESHOLD_COLOR) + 1 :]
    assert [line["points"] for _kind, line in thresholds] == [
        pytest.approx([0, high_y, 100, high_y]),
        pytest.approx([0, low_y, 100, low_y]),
    ]


def test_render_clears_and_draws_nothing_before_metadata_or_samples(graph_and_instructions):
    graph, instructions = graph_and_instructions
    config = GraphConfig("Y", 16, 24_000, 2.928, 0.488)
    graph.render(config, (Sample(1, 0, 0, 0, 0),), ())
    assert instructions

    graph.render(None, (Sample(1, 0, 0, 0, 0),), ())
    assert instructions == []

    graph.render(config, (), ())
    assert instructions == []
