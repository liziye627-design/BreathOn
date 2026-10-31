"""Continuous signed PCM16 little-endian conversion with retained soxr state."""

import math
import sys
from array import array
from typing import Final, final

import numpy as np
import soxr

from .errors import GatewayError

PCM16_MIN: Final = -32768
PCM16_MAX: Final = 32767


@final
class PCMLevels:
    """Aggregate signal levels without retaining samples or derived speech."""

    def __init__(self) -> None:
        self.samples = 0
        self.peak = 0
        self.clipped = 0
        self._sum = 0
        self._squares = 0

    def feed(self, pcm: bytes) -> None:
        if len(pcm) % 2:
            raise GatewayError("upstream_protocol")
        samples = array("h")
        samples.frombytes(pcm)
        if sys.byteorder != "little":
            samples.byteswap()
        if not samples:
            return
        self.samples += len(samples)
        chunk_peak = max(abs(sample) for sample in samples)
        self.peak = max(self.peak, chunk_peak)
        self.clipped += sum(sample in (PCM16_MIN, PCM16_MAX) for sample in samples)
        self._sum += sum(samples)
        self._squares += sum(sample * sample for sample in samples)

    @property
    def rms(self) -> float:
        """Return RMS amplitude in signed PCM16 units."""
        return math.sqrt(self._squares / self.samples) if self.samples else 0.0

    @property
    def mean(self) -> float:
        """Return DC offset in signed PCM16 units."""
        return self._sum / self.samples if self.samples else 0.0


@final
class PCMResampler:
    """Own one mutable filter per microphone stream or assistant response."""

    def __init__(self, source_rate: int, target_rate: int) -> None:
        self._stream = soxr.ResampleStream(source_rate, target_rate, 1, dtype="int16", quality="HQ")

    def feed(self, pcm: bytes) -> bytes:
        if len(pcm) % 2:
            raise GatewayError("upstream_protocol")
        samples = np.frombuffer(pcm, dtype="<i2").astype(np.int16, copy=False)
        return self._stream.resample_chunk(samples).astype("<i2", copy=False).tobytes()

    def finish(self) -> bytes:
        tail = self._stream.resample_chunk(np.empty(0, dtype=np.int16), last=True)
        return tail.astype("<i2", copy=False).tobytes()
