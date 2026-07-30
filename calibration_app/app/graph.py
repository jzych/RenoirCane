from __future__ import annotations


def timestamp_offset_us(timestamp_us: int, start_timestamp_us: int) -> int:
    return (timestamp_us - start_timestamp_us) & 0xFFFFFFFF


def impact_event_offsets_us(
    timestamps_us: tuple[int, ...], *, start_timestamp_us: int, end_timestamp_us: int
) -> tuple[int, ...]:
    span_us = timestamp_offset_us(end_timestamp_us, start_timestamp_us)
    return tuple(
        offset_us
        for timestamp_us in timestamps_us
        if (offset_us := timestamp_offset_us(timestamp_us, start_timestamp_us)) <= span_us
    )


def min_max_downsample(samples: list[tuple[int, float]], width: int) -> list[tuple[int, float]]:
    if width <= 0 or len(samples) <= width * 2:
        return samples
    output: list[tuple[int, float]] = []
    for bucket in range(width):
        start = bucket * len(samples) // width
        end = (bucket + 1) * len(samples) // width
        section = samples[start:end]
        low = min(section, key=lambda point: point[1])
        high = max(section, key=lambda point: point[1])
        output.extend(sorted((low, high), key=lambda point: point[0]))
    return output
