"""Synchronous event mapping never waits for media queues or device playback."""

from typing import assert_never, final

import anyio

from . import commands
from .buffers import MediaBudget, PriorityBuffer
from .config import MODEL, VOICE
from .diagnostics import log_response_failure
from .errors import GatewayError
from .events import (
    AudioDelta,
    AudioDone,
    Ignored,
    ResponseCreated,
    ResponseDone,
    Signal,
    TranscriptDelta,
    TranscriptDone,
    UserTranscript,
    parse_event,
)
from .turn import Turn
from .wire import Frame, ServerControl, bounded_text


@final
class Conversation:
    """Own mutable response generations and their bounded input/output queues."""

    def __init__(self) -> None:
        budget = MediaBudget()
        self.output = PriorityBuffer[Frame](budget, "speaker")
        self.input = PriorityBuffer[str](budget, "microphone")
        self.ready = anyio.Event()
        self.turn: Turn | None = None
        self.generation = 0
        self.device_generation = 0

    def audio_starting(self, generation: int) -> None:
        """Record audio_start committed by the sole writer, before awaiting send.

        A queued start can be discarded; an in-flight start cannot be retracted.
        Interrupts must follow that frame even while its send is blocked.
        """
        self.device_generation = generation

    def interrupt(self) -> None:
        turn = self.turn
        if turn is None or turn.interrupted:
            return
        turn.interrupted = True
        self.output.discard(turn.generation)
        if turn.generation == self.device_generation:
            self.output.control(
                ServerControl(type="interrupt").frame(turn.generation), generation=turn.generation
            )
        if turn.item_id is not None:
            self.input.control(commands.truncate(turn.item_id, turn.content_index))

    def handle_event(self, raw: str | bytes) -> None:
        event = parse_event(raw)
        match event:
            case Signal():
                self._signal(event)
            case ResponseCreated():
                if not self.ready.is_set():
                    raise GatewayError("upstream_protocol")
                self.interrupt()
                self.generation += 1
                self.turn = Turn(event.response.id, self.generation)
            case ResponseDone():
                self._done(event)
            case UserTranscript():
                self.output.control(
                    ServerControl(type="transcript", role="user", text=event.transcript).frame()
                )
            case AudioDelta() | AudioDone() | TranscriptDelta() | TranscriptDone():
                self._content(event)
            case Ignored():
                return
            case unreachable:
                assert_never(unreachable)

    def _signal(self, event: Signal) -> None:
        match event.type:
            case "session.updated":
                if not self.ready.is_set():
                    self.output.control(ServerControl(type="ready", model=MODEL, voice=VOICE).frame())
                    self.output.control(ServerControl(type="state", state="listening").frame())
                    self.input.control(commands.greeting())
                    self.ready.set()
            case "input_audio_buffer.speech_started":
                self.interrupt()
                self.output.control(ServerControl(type="state", state="listening").frame())
            case "input_audio_buffer.speech_stopped":
                self.output.control(ServerControl(type="state", state="thinking").frame())
            case "error" | "conversation.item.input_audio_transcription.failed":
                raise GatewayError("upstream_error")
            case unreachable:
                assert_never(unreachable)

    def _done(self, event: ResponseDone) -> None:
        turn = self.turn
        if turn is None or event.response.id != turn.response_id:
            return
        match event.response.status:
            case "completed":
                if not turn.interrupted:
                    turn.complete(self.output)
            case "cancelled":
                self.interrupt()
                self.output.control(ServerControl(type="state", state="listening").frame())
            case "failed" | "incomplete" | "in_progress":
                log_response_failure(event.response, turn.generation)
                raise GatewayError("response_failed")
            case unreachable:
                assert_never(unreachable)

    def _content(self, event: AudioDelta | AudioDone | TranscriptDelta | TranscriptDone) -> None:
        turn = self.turn
        if turn is None or event.response_id != turn.response_id or turn.interrupted:
            return
        turn.identify(event)
        match event:
            case AudioDelta():
                turn.audio(event, self.output)
            case AudioDone():
                turn.finish_audio(self.output)
            case TranscriptDelta():
                turn.transcript = bounded_text(turn.transcript + event.delta)
                self.output.control(
                    ServerControl(type="transcript", role="assistant", text=turn.transcript).frame(
                        turn.generation
                    ),
                    generation=turn.generation,
                )
            case TranscriptDone():
                turn.transcript = bounded_text(event.transcript)
                self.output.control(
                    ServerControl(type="transcript", role="assistant", text=turn.transcript).frame(
                        turn.generation
                    ),
                    generation=turn.generation,
                )
            case unreachable:
                assert_never(unreachable)
