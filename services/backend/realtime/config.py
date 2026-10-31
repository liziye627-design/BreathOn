"""Environment-only CLI settings; no credential files are read."""

import os
from collections.abc import Mapping
from typing import ClassVar, Final

from pydantic import BaseModel, ConfigDict, Field, SecretStr, ValidationError

from .errors import GatewayError

MODEL: Final = "gpt-realtime-2.1"
VOICE: Final = "marin"
ENDPOINT: Final = f"wss://api.openai.com/v1/realtime?model={MODEL}"


class Settings(BaseModel):
    """Secret fields never appear in repr or error controls."""

    model_config: ClassVar[ConfigDict] = ConfigDict(frozen=True, extra="forbid")
    api_key: SecretStr = Field(min_length=1, repr=False)
    device_token: SecretStr = Field(min_length=1, max_length=1024, repr=False)
    host: str = "127.0.0.1"
    port: int = Field(default=8766, ge=1, le=65535)
    interface_index: int | None = Field(default=None, ge=1, le=4294967295)
    max_session_seconds: float = Field(default=180, gt=0, le=180, allow_inf_nan=False)
    hello_timeout: float = Field(default=5, gt=0, le=5)
    upstream_timeout: float = Field(default=15, gt=0, le=30)

    @classmethod
    def from_env(cls, environ: Mapping[str, str] | None = None) -> "Settings":
        values = os.environ if environ is None else environ
        key = values.get("OPENAI_API_KEY", "").strip()
        token = values.get("BREATHON_DEVICE_TOKEN", "").strip()
        if not key or not token:
            raise GatewayError("credentials_missing")
        if (
            values.get("BREATHON_REALTIME_MODEL", MODEL) != MODEL
            or values.get("BREATHON_REALTIME_VOICE", VOICE) != VOICE
        ):
            raise GatewayError("invalid_config")
        try:
            return cls(
                api_key=SecretStr(key),
                device_token=SecretStr(token),
                host=values.get("BREATHON_BIND_HOST", "127.0.0.1"),
                port=int(values.get("BREATHON_PORT", "8766")),
                interface_index=(
                    int(values["BREATHON_BIND_INTERFACE"]) if values.get("BREATHON_BIND_INTERFACE") else None
                ),
                max_session_seconds=float(values.get("BREATHON_MAX_SESSION_SECONDS", "180")),
            )
        except (ValidationError, ValueError):
            raise GatewayError("invalid_config") from None
