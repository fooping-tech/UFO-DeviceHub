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
from .protocol import CommandResult, DeviceEvent, DeviceId, EffectId, LightMode, OperationMode, ProtocolError

__all__ = [
    "CommandResult",
    "DeviceEvent",
    "DeviceGateway",
    "DeviceId",
    "EffectId",
    "GatewayError",
    "GatewayNotConnected",
    "LightMode",
    "LineTransport",
    "OperationMode",
    "ProtocolError",
    "SerialLineTransport",
    "TransportError",
]
