"""Real stateful soxr conversion, no upstream calls."""

import numpy as np
import pytest

from backend.realtime.audio import PCMResampler


@pytest.mark.parametrize("rates", [(16000, 24000), (24000, 16000)])
def test_resampling_when_fragmented(rates: tuple[int, int]) -> None:
    # Given one second of PCM, with deliberately irregular chunks.
    source, target = rates
    signal = (np.sin(np.arange(source) * 0.1) * 10000).astype("<i2").tobytes()
    converter = PCMResampler(source, target)
    reference = PCMResampler(source, target)
    # When streaming through one retained filter state.
    actual = b"".join(converter.feed(signal[i : i + 126]) for i in range(0, len(signal), 126))
    actual += converter.finish()
    # Then the count and waveform match the continuous conversion.
    assert len(actual) == target * 2
    # soxr's int16 path dithers independently; tolerate only quantization noise.
    expected = reference.feed(signal) + reference.finish()
    difference = np.frombuffer(actual, dtype="<i2").astype(np.int32) - np.frombuffer(expected, dtype="<i2")
    assert np.max(np.abs(difference)) <= 2
