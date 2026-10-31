"""Parse the relevant official Realtime events once at the upstream boundary."""

from dataclasses import dataclass
from typing import Annotated, ClassVar, Final, Literal

from pydantic import BaseModel, ConfigDict, Field, TypeAdapter, ValidationError

from .errors import GatewayError


class EventModel(BaseModel):
    model_config: ClassVar[ConfigDict] = ConfigDict(frozen=True, extra="ignore", strict=True)


class Envelope(EventModel):
    type: str


class Signal(EventModel):
    type: Literal[
        "session.updated",
        "input_audio_buffer.speech_started",
        "input_audio_buffer.speech_stopped",
        "error",
        "conversation.item.input_audio_transcription.failed",
    ]


class ResponseErrorDetails(EventModel):
    """Retain only the machine code; never retain error.message or other fields."""

    code: str | None = None


class ResponseStatusDetails(EventModel):
    """Selected status fields; diagnostics map all unrecognized values to unknown."""

    type: str | None = None
    reason: str | None = None
    error: ResponseErrorDetails | None = None


class ResponseInfo(EventModel):
    id: str = Field(min_length=1, max_length=256)
    status: Literal["in_progress", "completed", "cancelled", "failed", "incomplete"]
    status_details: ResponseStatusDetails | None = None


class ResponseEvent(EventModel):
    response: ResponseInfo


class ResponseCreated(ResponseEvent):
    type: Literal["response.created"]


class ResponseDone(ResponseEvent):
    type: Literal["response.done"]


class AudioIdentity(EventModel):
    response_id: str = Field(min_length=1, max_length=256)
    item_id: str = Field(min_length=1, max_length=256)
    content_index: int = Field(ge=0)


class AudioDelta(AudioIdentity):
    type: Literal["response.output_audio.delta"]
    delta: str


class AudioDone(AudioIdentity):
    type: Literal["response.output_audio.done"]


class TranscriptDelta(AudioIdentity):
    type: Literal["response.output_audio_transcript.delta"]
    delta: str


class TranscriptDone(AudioIdentity):
    type: Literal["response.output_audio_transcript.done"]
    transcript: str


class UserTranscript(EventModel):
    type: Literal["conversation.item.input_audio_transcription.completed"]
    transcript: str


@dataclass(frozen=True, slots=True)
class Ignored:
    """Unneeded official event (e.g. rate limits); carries no retained payload."""


KnownEvent = (
    Signal
    | ResponseCreated
    | ResponseDone
    | AudioDelta
    | AudioDone
    | TranscriptDelta
    | TranscriptDone
    | UserTranscript
)
Event = KnownEvent | Ignored
_EVENT: Final = TypeAdapter(Annotated[KnownEvent, Field(discriminator="type")])
_KINDS: Final = frozenset(
    [
        "session.updated",
        "input_audio_buffer.speech_started",
        "input_audio_buffer.speech_stopped",
        "error",
        "conversation.item.input_audio_transcription.failed",
        "response.created",
        "response.done",
        "response.output_audio.delta",
        "response.output_audio.done",
        "response.output_audio_transcript.delta",
        "response.output_audio_transcript.done",
        "conversation.item.input_audio_transcription.completed",
    ]
)


def parse_event(raw: str | bytes) -> Event:
    """Accept known events strictly and discard unused official event types."""
    if len(raw) > 256 * 1024:
        raise GatewayError("upstream_protocol")
    try:
        header = Envelope.model_validate_json(raw)
        if header.type not in _KINDS:
            return Ignored()
        return _EVENT.validate_json(raw)
    except ValidationError:
        raise GatewayError("upstream_protocol") from None
