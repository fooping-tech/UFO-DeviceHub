"""UFO Game device hub client (PC side).

    from pc import DeviceGateway, DeviceId
"""

from .device_gateway import (
    DeviceGateway,
    GatewayError,
    GatewayNotConnected,
    LineTransport,
    SerialLineTransport,
    TransportError,
)
from .protocol import (
    CommandResult,
    DeviceEvent,
    DeviceId,
    DialDirection,
    DialEffect,
    DialLedTarget,
    DialRequest,
    RemoteButton,
    RemotePose,
    StaffAction,
    EffectId,
    EffectPriority,
    LightMode,
    OperationMode,
    ProtocolError,
)
from .trace import TraceLog

__all__ = [
    "CommandResult",
    "DeviceEvent",
    "DeviceGateway",
    "DeviceId",
    "DialDirection",
    "DialEffect",
    "DialLedTarget",
    "DialRequest",
    "RemoteButton",
    "RemotePose",
    "StaffAction",
    "EffectId",
    "EffectPriority",
    "GatewayError",
    "GatewayNotConnected",
    "LightMode",
    "LineTransport",
    "OperationMode",
    "ProtocolError",
    "SerialLineTransport",
    "TraceLog",
    "TransportError",
]
