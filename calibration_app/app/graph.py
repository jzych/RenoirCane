from __future__ import annotations


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
