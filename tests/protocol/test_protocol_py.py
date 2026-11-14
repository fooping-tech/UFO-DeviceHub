"""Python protocol codec: golden vectors, validation, JSON framing."""

import json
from pathlib import Path

import pytest

from pc import protocol as P
from pc.protocol import Packet, ProtocolError

VECTORS = json.loads((Path(__file__).parent / "vectors.json").read_text())["vectors"]


@pytest.mark.parametrize("v", VECTORS, ids=[v["name"] for v in VECTORS])
def test_golden_vectors(v):
    raw = bytes.fromhex(v["hex"])
    if "error" in v:
        with pytest.raises(ProtocolError, match=v["error"]):
            Packet.decode(raw)
        return
    p = Packet.decode(raw, P.system_magic(v["system_id"]))
    assert (p.source, p.destination, p.message_type) == (v["source"], v["destination"], v["message_type"])
    assert (p.sequence, p.session_id, p.command_id) == (v["sequence"], v["session_id"], v["command_id"])
    assert p.payload.hex() == v["payload_hex"]
    assert p.encode() == raw


def test_header_layout_little_endian():
    raw = Packet(1, 0x20, 1, 0x1234, 0xA1B2C3D4, 0x01020304, b"\x22").encode()
    assert raw.hex() == "4655010120013412d4c3b2a1040302010122"
    assert len(raw) == P.HEADER_SIZE + 1


def test_payload_limits():
    Packet(1, 2, 1, payload=bytes(P.MAX_PAYLOAD)).encode()
    with pytest.raises(ProtocolError):
        Packet(1, 2, 1, payload=bytes(P.MAX_PAYLOAD + 1)).encode()
    with pytest.raises(ProtocolError):
        Packet(1, 2, 0x07).encode()


@pytest.mark.parametrize(
    "name,params",
    [
        ("SET_LIGHT_MODE", {"mode": "DISCO"}),
        ("SET_LIGHT_MODE", {"mode": "IDLE", "brightness": 256}),
        ("SET_LIGHT_MODE", {"mode": "IDLE", "color": [1, 2]}),
        ("SET_LIGHT_MODE", {"mode": "IDLE", "color": "#12345"}),
        ("TRIGGER_EFFECT", {"effect": "NONE"}),
        ("TRIGGER_EFFECT", {"effect": 99}),
        ("RELAY_PULSE", {}),
        ("RELAY_PULSE", {"duration_ms": 0}),
        ("RELAY_PULSE", {"duration_ms": 70000}),
        ("RELAY_PULSE", {"duration_ms": True}),
        ("SET_MODE", {"mode": "PARTY"}),
        ("SET_MIST", {}),
        ("SET_MIST", {"pattern": "FOG"}),
        ("SET_MIST", {"pattern": "PUFF", "duration_s": 70000}),
        ("SET_LIGHT_MODE", {"mode": 14}),
        ("RELEASE_BALL", {"angle": 90}),  # no wire parameters for angles
        ("SESSION_BEGIN", {}),  # hub only
        ("SELF_DESTRUCT", {}),
    ],
)
def test_invalid_commands_rejected(name, params):
    with pytest.raises(ProtocolError):
        P.normalize_command(name, params)


def test_normalize_defaults_and_aliases():
    assert P.normalize_command("SET_LIGHT_MODE", {"mode": "game_play", "color": "#0a0b0c"}) == {
        "mode": "GAME_PLAY",
        "brightness": 128,
        "transition_ms": 0,
        "speed": 128,
        "color": [10, 11, 12],
    }
    assert P.normalize_command("TRIGGER_EFFECT", {"effect_id": 7})["effect"] == "SCORE"
    assert P.normalize_command("RELAY_OFF") == {"relay_id": 255}
    assert P.normalize_command("SET_MODE", {"mode": P.OperationMode.GAME}) == {"mode": "GAME"}


def test_command_message_and_ids():
    m = P.command(0x10, 5, 1001, "SET_LIGHT_MODE", {"mode": "GAME_PLAY", "brightness": 180})
    assert m["protocol_version"] == 1 and m["type"] == "COMMAND" and m["destination"] == 16
    with pytest.raises(ProtocolError):
        P.command(0x10, 5, 0, "STOP_ALL")
    with pytest.raises(ProtocolError):
        P.command(0x10, 5, P.HUB_COMMAND_BIT, "STOP_ALL")


def test_line_framing():
    assert P.decode_line(b'{"type":"HEARTBEAT"}\r\n') == {"type": "HEARTBEAT"}
    for bad in (b"", b"{", b"[1]", b'{"x":1}', b"\xff\xfe"):
        with pytest.raises(ProtocolError):
            P.decode_line(bad)
    with pytest.raises(ProtocolError):
        P.encode_line({"type": "X", "pad": "a" * 600})
    assert "\n" not in P.encode_line({"type": "X", "s": "a\nb"})


def test_command_payload_encoding_matches_hub_layout():
    assert P.encode_command_payload("RELAY_PULSE", {"duration_ms": 1500}).hex() == "1200dc05"
    assert P.encode_command_payload("SET_MODE", {"mode": "GAME"}).hex() == "0501"
    for code, n in P.COMMAND_PARAM_LENGTH.items():
        if code.name in P.PC_COMMANDS:
            params = {
                "SET_MODE": {"mode": "GAME"},
                "SET_LIGHT_MODE": {"mode": "IDLE"},
                "TRIGGER_EFFECT": {"effect": "SCORE"},
                "RELAY_PULSE": {"duration_ms": 1},
                "SET_MIST": {"pattern": "PUFF"},
            }.get(code.name, {})
            assert len(P.encode_command_payload(code.name, params)) == 1 + n, code.name


def test_set_mist_and_new_light_modes():
    assert P.normalize_command("SET_MIST", {"pattern": "breath"}) == {"pattern": "BREATH", "duration_s": 0}
    assert P.encode_command_payload("SET_MIST", {"pattern": "PUFF", "duration_s": 300}).hex() == "14032c01"
    assert P.normalize_command("SET_LIGHT_MODE", {"mode": "MAGIC_PUFF"})["mode"] == "MAGIC_PUFF"
    assert P.encode_event_payload(P.EventCode.MIST_DONE, on_s=95, reason=0).hex() == "215f0000"


def test_system_magic_and_hello():
    assert P.system_magic(1) == 0x5546 and P.system_magic(2) == 0x5547
    raw = Packet(1, 0x20, 1, payload=b"\x04", magic=P.system_magic(2)).encode()
    with pytest.raises(ProtocolError, match="bad_magic"):
        Packet.decode(raw)  # a system 1 decoder
    assert Packet.decode(raw, P.system_magic(2)).payload == b"\x04"
    assert P.hello(5, 2)["system_id"] == 2
    assert "system_id" not in P.hello(5)
    with pytest.raises(ProtocolError):
        P.hello(5, 0)
