from app.graph import min_max_downsample


def test_min_max_downsampling_preserves_positive_and_negative_spikes():
    samples = [(index, 0.01) for index in range(3000)]
    samples[777] = (777, 12.0)
    samples[1777] = (1777, -11.0)

    output = min_max_downsample(samples, width=30)
    values = [value for _, value in output]

    assert 12.0 in values
    assert -11.0 in values
