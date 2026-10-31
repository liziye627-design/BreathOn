"""Version-one frames and bounded JSON controls at the LAN trust boundary."""

from dataclasses import dataclass
from enum import IntEnum
from typing import Annotated, ClassVar, Final, Literal

from anyio import EndOfStream, IncompleteRead
from anyio.streams.buffered import BufferedByteReceiveStream
from pydantic import BaseModel, ConfigDict, Field, SecretStr, TypeAdapter, ValidationError

from .errors import ErrorCode, GatewayError

MAX_PAYLOAD: Final = 4096
TRANSCRIPT_BYTES: Final = 512
MAX_GENERATION: Final = 0xFFFFFFFF


class Kind(IntEnum):
    """Wire directions; speaker frames are never accepted from the board."""

    CONTROL = 1
    MICROPHONE = 2
    SPEAKER = 3


@dataclass(frozen=True, slots=True)
class Frame:
    """An immutable validated frame."""

    kind: Kind
    payload: bytes
    generation: int = 0

    def __post_init__(self) -> None:
        if not 0 <= self.generation <= MAX_GENERATION or len(self.payload) > MAX_PAYLOAD:
            raise GatewayError("invalid_frame")
        if self.kind != Kind.CONTROL and (not self.payload or len(self.payload) % 2):
            raise GatewayError("invalid_frame")

    def encode(self) -> bytes:
        return (
            b"BR\x01"
            + bytes([self.kind])
            + len(self.payload).to_bytes(4, "big")
            + self.generation.to_bytes(4, "big")
            + self.payload
        )


async def read_frame(reader: BufferedByteReceiveStream) -> Frame:
    """Reject bad headers before allocating or awaiting their payloads."""
    try:
        header = await reader.receive_exactly(12)
        if header[:3] != b"BR\x01":
            raise GatewayError("invalid_frame")
        try:
            kind = Kind(header[3])
        except ValueError:
            raise GatewayError("invalid_frame") from None
        length = int.from_bytes(header[4:8], "big")
        if length > MAX_PAYLOAD or (kind != Kind.CONTROL and (not length or length % 2)):
            raise GatewayError("invalid_frame")
        return Frame(kind, await reader.receive_exactly(length), int.from_bytes(header[8:], "big"))
    except (EndOfStream, IncompleteRead):
        raise GatewayError("device_disconnected") from None


class ClientControl(BaseModel):
    """Strict client fields; misspelled commands fail closed."""

    model_config: ClassVar[ConfigDict] = ConfigDict(frozen=True, extra="forbid", strict=True)


class Hello(ClientControl):
    """A locally paired device, not an OpenAI credential."""

    type: Literal["hello"]
    token: SecretStr = Field(min_length=1, max_length=1024, repr=False)
    device_id: str = Field(min_length=1, max_length=128)
    sample_rate: Literal[16000]


class Stop(ClientControl):
    type: Literal["stop"]


class Ping(ClientControl):
    type: Literal["ping"]


class Interrupted(ClientControl):
    type: Literal["interrupted"]


ClientMessage = Hello | Stop | Ping | Interrupted
_CLIENT: Final = TypeAdapter(Annotated[ClientMessage, Field(discriminator="type")])


def parse_control(frame: Frame) -> ClientMessage:
    """Parse client controls without retaining raw validation errors."""
    try:
        return _CLIENT.validate_json(frame.payload)
    except ValidationError:
        raise GatewayError("invalid_control") from None


def bounded_text(text: str) -> str:
    """Keep a valid UTF-8 prefix; 512 bytes also bounds JSON escaping to 3072."""
    return text.encode("utf-8", errors="replace")[:TRANSCRIPT_BYTES].decode("utf-8", errors="ignore")


class ServerControl(BaseModel):
    """Outbound controls contain only bounded, intentional public fields."""

    model_config: ClassVar[ConfigDict] = ConfigDict(frozen=True)
    type: Literal[
        "ready", "audio_start", "audio_end", "interrupt", "state", "transcript", "error", "closed", "pong"
    ]
    state: Literal["listening", "thinking", "speaking", "stopped"] | None = None
    role: Literal["user", "assistant"] | None = None
    text: str | None = None
    model: str | None = None
    voice: str | None = None
    code: ErrorCode | None = None

    def frame(self, generation: int = 0) -> Frame:
        control = self
        if self.text is not None:
            control = self.model_copy(update={"text": bounded_text(self.text)})
        return Frame(Kind.CONTROL, control.model_dump_json(exclude_none=True).encode(), generation)
