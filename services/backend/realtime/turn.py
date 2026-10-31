"""One response generation and its unplayed PCM/filter/transcript state."""

import base64
import binascii
from typing import final

from .audio import PCMResampler
from .buffers import PriorityBuffer
from .errors import GatewayError
from .events import AudioDelta, AudioIdentity
from .wire import Frame, Kind, ServerControl


@final
class Turn:
    """Mutable per-response state; never reused after interruption."""

    def __init__(self, response_id: str, generation: int) -> None:
        self.response_id = response_id
        self.generation = generation
        self.item_id: str | None = None
        self.content_index = 0
        self.interrupted = False
        self.finished = False
        # Queued audio_start, not evidence that the device writer has sent it.
        self.started = False
        self.audio_finished = False
        self.transcript = ""
        self._converter = PCMResampler(24000, 16000)
        self._pending = b""

    def identify(self, event: AudioIdentity) -> None:
        if self.item_id is not None and (self.item_id, self.content_index) != (
            event.item_id,
            event.content_index,
        ):
            raise GatewayError("upstream_protocol")
        self.item_id, self.content_index = event.item_id, event.content_index

    def audio(self, event: AudioDelta, output: PriorityBuffer[Frame]) -> None:
        if self.audio_finished or self.finished:
            return
        self.identify(event)
        try:
            pcm = base64.b64decode(event.delta, validate=True)
        except (binascii.Error, ValueError):
            raise GatewayError("upstream_protocol") from None
        self._emit(self._converter.feed(pcm), output)

    def _emit(self, pcm: bytes, output: PriorityBuffer[Frame], *, final: bool = False) -> None:
        pending = self._pending + pcm
        end = len(pending) if final else len(pending) // 640 * 640
        if end and not self.started:
            self.started = True
            output.control(
                ServerControl(type="audio_start").frame(self.generation), generation=self.generation
            )
            output.control(
                ServerControl(type="state", state="speaking").frame(self.generation),
                generation=self.generation,
            )
        for offset in range(0, end, 640):
            chunk = pending[offset : min(offset + 640, end)]
            output.media(
                Frame(Kind.SPEAKER, chunk, self.generation),
                size=len(chunk),
                generation=self.generation,
                duration=len(chunk) / 32000,
            )
        self._pending = pending[end:]

    def finish_audio(self, output: PriorityBuffer[Frame]) -> None:
        if not self.audio_finished:
            self.audio_finished = True
            self._emit(self._converter.finish(), output, final=True)

    def complete(self, output: PriorityBuffer[Frame]) -> None:
        if self.finished:
            return
        self.finish_audio(output)
        self.finished = True
        if self.started:
            output.media(
                ServerControl(type="audio_end").frame(self.generation), size=0, generation=self.generation
            )
            output.media(
                ServerControl(type="state", state="listening").frame(self.generation),
                size=0,
                generation=self.generation,
            )
        else:
            output.control(ServerControl(type="state", state="listening").frame())
