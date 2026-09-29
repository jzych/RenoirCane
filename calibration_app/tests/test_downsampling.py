from app.graph import impact_event_offsets_us, min_max_downsample, timestamp_offset_us


def test_min_max_downsampling_preserves_positive_and_negative_spikes():
    samples = [(index, 0.01) for index in range(3000)]
    samples[777] = (777, 12.0)
    samples[1777] = (1777, -11.0)

    output = min_max_downsample(samples, width=30)
    values = [value for _, value in output]

    assert 12.0 in values
    assert -11.0 in values


def test_timestamp_offsets_stay_monotonic_across_uint32_wrap():
    start = 0xFFFFFFF0

    assert timestamp_offset_us(start, start) == 0
    assert timestamp_offset_us(0xFFFFFFF8, start) == 8
    assert timestamp_offset_us(0x00000010, start) == 32


def test_impact_event_offsets_include_only_events_in_the_visible_sample_window():
    assert impact_event_offsets_us(
        (0xFFFFFFF8, 0x00000008, 0x00000030),
        start_timestamp_us=0xFFFFFFF0,
        end_timestamp_us=0x00000010,
    ) == (8, 24)


def test_min_max_downsampling_keeps_points_in_time_order():
    # In every bucket the maximum comes before the minimum.
    samples = [(index, float(-index if index % 100 > 50 else index)) for index in range(3000)]

    output = min_max_downsample(samples, width=30)

    assert len(output) == 60
    assert [time for time, _ in output] == sorted(time for time, _ in output)


def test_min_max_downsampling_returns_short_input_unchanged():
    samples = [(index, float(index)) for index in range(60)]

    assert min_max_downsample(samples, width=30) is samples
