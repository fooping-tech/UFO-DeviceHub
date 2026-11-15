"""Single source of truth check: every enum in shared/messages.h and
shared/device_ids.h must exist with the same value in pc/protocol.py (and the
reverse), and the timing / size constants must agree."""

import re
from enum import Enum
from pathlib import Path

import pytest

from pc import protocol as P

ROOT = Path(__file__).resolve().parents[2]
ENUM_RE = re.compile(r"enum class (\w+) : uint8_t \{(.*?)\};", re.S)
ENTRY_RE = re.compile(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+),", re.M)

# C++ enum name -> Python enum
MAPPING = {
    "MsgType": P.MsgType,
    "CommandCode": P.CommandCode,
    "EventCode": P.EventCode,
    "AckResult": P.AckResult,
    "AckFlag": P.AckFlag,
    "DeviceType": P.DeviceType,
    "OperationMode": P.OperationMode,
    "LightMode": P.LightMode,
    "MistPattern": P.MistPattern,
    "MistStatusFlag": P.MistStatusFlag,
    "EffectId": P.EffectId,
    "DialState": P.DialState,
    "DialEffect": P.DialEffect,
    "DialDirection": P.DialDirection,
    "EffectPriority": P.EffectPriority,
    "DialLedTarget": P.DialLedTarget,
    "DialLedFlag": P.DialLedFlag,
    "IlluminationState": P.IlluminationState,
    "ReleaseFailReason": P.ReleaseFailReason,
    "RelayEndReason": P.RelayEndReason,
    "ErrorCode": P.ErrorCode,
    "NodeFlag": P.NodeFlag,
    "DeviceId": P.DeviceId,
}


def snake(name: str) -> str:
    s = re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", name)
    s = re.sub(r"(?<=[A-Za-z])(\d)", r"_\1", s)  # M5Dial1 -> M5_DIAL_1
    return s.upper().replace("M_5_DIAL", "M5DIAL")


def cpp_enums() -> dict[str, dict[str, int]]:
    out = {}
    for f in ("shared/protocol.h", "shared/messages.h", "shared/device_ids.h"):
        for name, body in ENUM_RE.findall((ROOT / f).read_text()):
            out[name] = {snake(k): int(v, 0) for k, v in ENTRY_RE.findall(body)}
    return out


CPP = cpp_enums()


# C++-only enums that never cross the wire as numbers (decode errors are
# reported by name, see decodeErrorName() / Packet.decode()).
CPP_ONLY = {"DecodeError"}


def test_all_cpp_enums_are_mirrored():
    assert set(CPP) - CPP_ONLY == set(MAPPING), "enum added on one side only"


@pytest.mark.parametrize("cpp_name", sorted(MAPPING))
def test_enum_values_match(cpp_name):
    py: type[Enum] = MAPPING[cpp_name]
    py_values = {m.name: int(m.value) for m in py}
    assert CPP[cpp_name] == py_values


def test_constants_match():
    proto = (ROOT / "shared/protocol.h").read_text()
    assert f"kMagic = 0x{P.MAGIC:04X}" in proto
    assert f"kMaxSystemId = {P.MAX_SYSTEM_ID};" in proto
    assert f"kHeaderSize = {P.HEADER_SIZE};" in proto
    assert f"kMaxPacketSize = {P.MAX_PACKET_SIZE};" in proto
    link = (ROOT / "shared/link_config.h").read_text()
    assert f"#define UFO_SERIAL_MAX_LINE {P.MAX_LINE}" in link


def test_param_lengths_match_firmware():
    src = (ROOT / "shared/messages.cpp").read_text()
    body = src[src.index("int commandParamLength") :]
    body = body[: body.index("return -1;")]
    lengths = {}
    pending = []
    for line in body.splitlines():
        m = re.search(r"case CommandCode::(\w+):(?:\s*return (\d+);)?", line)
        if m:
            pending.append(snake(m.group(1)))
            if m.group(2):
                for n in pending:
                    lengths[n] = int(m.group(2))
                pending = []
        else:
            r = re.search(r"return (\d+);", line)
            if r and pending:
                for n in pending:
                    lengths[n] = int(r.group(1))
                pending = []
    assert lengths == {c.name: n for c, n in P.COMMAND_PARAM_LENGTH.items()}


def test_no_session_commands_match_firmware():
    src = (ROOT / "shared/messages.cpp").read_text()
    body = src[src.index("bool commandRequiresSession") :]
    body = body[: body.index("return false;")]
    names = {snake(n) for n in re.findall(r"case CommandCode::(\w+):", body)}
    assert names - {"SESSION_BEGIN", "SESSION_END"} == P.NO_SESSION_COMMANDS


def test_ack_and_event_names_match_hub_json():
    src = (ROOT / "shared/messages.cpp").read_text()
    ack_names = set(re.findall(r'case AckResult::\w+: return "(\w+)";', src))
    assert ack_names == {m.name for m in P.AckResult}
    event_names = set(re.findall(r'case EventCode::\w+: return "(\w+)";', src))
    assert event_names == set(P.EVENT_WIRE_NAMES.values())
