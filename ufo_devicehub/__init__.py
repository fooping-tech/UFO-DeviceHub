"""UFO device hub, PC side: the import name other projects use.

    from ufo_devicehub import DeviceGateway, MockHub, MockConfig, TraceLog
    from ufo_devicehub import protocol as P

This is the public API. The code lives in ``pc`` (``from pc import ...`` and
``python -m pc.web_console`` keep working); a project that imports from here does not
depend on where it lives. What is not named here (names starting with ``_``, the mock's
device classes, the web console) may change without notice.

For a caller that owns the PC's serial ports (a game with another controller on USB):

- ``SerialLineTransport(port, exclude=..., exclusive=True)``: ports that are never opened,
  and the hub's port for this program only.
- ``DeviceGateway.link_diagnostics()``: why there is no session, read only.
- ``DeviceGateway(trace=TraceLog(path).record)``: every command, ACK and event.
"""

from pc import device_gateway, game_mode, mock_hub, protocol, trace
from pc.device_gateway import (
    DeviceGateway,
    GatewayError,
    GatewayNotConnected,
    LineTransport,
    SerialLineTransport,
    TransportError,
    guess_port,
    guess_ports,
)
from pc.mock_hub import MockConfig, MockHub
from pc.protocol import (
    MAX_SYSTEM_ID,
    PROTOCOL_VERSION,
    CommandResult,
    DeviceEvent,
    DeviceId,
    DialDirection,
    DialEffect,
    DialLedTarget,
    EffectId,
    EffectPriority,
    LightMode,
    MistPattern,
    OperationMode,
    ProtocolError,
)
from pc.trace import TraceLog

__all__ = [
    "MAX_SYSTEM_ID",
    "PROTOCOL_VERSION",
    "CommandResult",
    "DeviceEvent",
    "DeviceGateway",
    "DeviceId",
    "DialDirection",
    "DialEffect",
    "DialLedTarget",
    "EffectId",
    "EffectPriority",
    "GatewayError",
    "GatewayNotConnected",
    "LightMode",
    "LineTransport",
    "MistPattern",
    "MockConfig",
    "MockHub",
    "OperationMode",
    "ProtocolError",
    "SerialLineTransport",
    "TraceLog",
    "TransportError",
    "device_gateway",
    "game_mode",
    "guess_port",
    "guess_ports",
    "mock_hub",
    "protocol",
    "trace",
]
