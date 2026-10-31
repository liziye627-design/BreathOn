# Narrow annotations for the installed soxr 1.1 streaming int16 API.

from typing import Literal

import numpy as np
from numpy.typing import NDArray

class ResampleStream:
    def __init__(
        self,
        in_rate: float,
        out_rate: float,
        num_channels: int,
        dtype: Literal["int16"],
        quality: Literal["HQ"],
    ) -> None: ...
    def resample_chunk(self, x: NDArray[np.int16], last: bool = False) -> NDArray[np.int16]: ...
