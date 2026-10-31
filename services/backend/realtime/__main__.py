"""Run explicitly with python -m backend.realtime [serve]; configuration is ENV-only."""

import logging
import sys

import anyio

from .config import Settings
from .errors import GatewayError
from .gateway import serve


def main() -> int:
    """Run the explicit ENV-only service entry point."""
    if sys.argv[1:] in (["--help"], ["-h"]):
        sys.stdout.write(
            "Usage: python -m backend.realtime [serve]\n"
            "ENV: OPENAI_API_KEY, BREATHON_DEVICE_TOKEN, BREATHON_BIND_HOST,\n"
            "     BREATHON_PORT (8766), BREATHON_MAX_SESSION_SECONDS (180),\n"
            "     BREATHON_BIND_INTERFACE (optional Windows interface index),\n"
            "     BREATHON_REALTIME_MODEL (gpt-realtime-2.1), BREATHON_REALTIME_VOICE (marin).\n"
        )
        return 0
    if sys.argv[1:] not in ([], ["serve"]):
        sys.stderr.write("realtime: invalid arguments; use --help\n")
        return 2
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    try:
        settings = Settings.from_env()
        anyio.run(serve, settings, backend="asyncio")
    except GatewayError as error:
        sys.stderr.write(f"realtime: {error.code}\n")
        return 2
    except OSError:
        sys.stderr.write("realtime: listener_unavailable\n")
        return 2
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
