"""Audio diagnostics retain aggregate numbers, never microphone content."""

import math
import struct

import pytest

from backend.realtime.audio import PCMLevels
from backend.realtime.errors import GatewayError


def test_levels_preserve_signed_extremes_and_chunk_boundaries() -> None:
    values = [-32768, 32767, 0, -1000, 1000]
    pcm = struct.pack("<5h", *values)
    together, split = PCMLevels(), PCMLevels()
    together.feed(pcm)
    for offset in range(0, len(pcm), 2):
        split.feed(pcm[offset : offset + 2])
    for meter in (together, split):
        assert meter.samples == 5
        assert meter.peak == 32768
        assert meter.clipped == 2
        assert meter.mean == pytest.approx(-0.2)
        assert meter.rms == pytest.approx(math.sqrt(sum(x * x for x in values) / 5))


def test_silence_and_empty_stream_have_zero_levels() -> None:
    meter = PCMLevels()
    assert (meter.rms, meter.mean, meter.peak, meter.samples) == (0, 0, 0, 0)
    meter.feed(bytes(640))
    assert (meter.rms, meter.mean, meter.peak, meter.samples) == (0, 0, 0, 320)


def test_partial_sample_is_rejected_without_changing_aggregates() -> None:
    meter = PCMLevels()
    with pytest.raises(GatewayError):
        meter.feed(b"\x00")
    assert meter.samples == 0
