"""Machine-consumed session parameters and retained resource ceilings."""

from openai.types.realtime import SessionUpdateEventParam
from pydantic import TypeAdapter

from backend.realtime.commands import configure
from backend.realtime.config import Settings


def test_response_limit_when_configuring_session() -> None:
    # Given the serialized configuration that is actually sent upstream.
    # When it is parsed using the official SDK's client event schema.
    event = TypeAdapter(SessionUpdateEventParam).validate_json(configure())
    session = event["session"]
    # Then ordinary replies have room beyond the observed 256-token exhaustion.
    assert session["type"] == "realtime"
    assert "max_output_tokens" in session
    assert session["max_output_tokens"] == 1024


def test_session_duration_when_defaults_used() -> None:
    # Given only fake required credentials, without a duration override.
    values = {"OPENAI_API_KEY": "fake", "BREATHON_DEVICE_TOKEN": "fake"}
    # When the production ENV loader selects defaults.
    settings = Settings.from_env(values)
    # Then raising the response token ceiling does not extend the paid session.
    assert settings.max_session_seconds == 180
