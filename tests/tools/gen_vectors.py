"""Regenerates tests/protocol/vectors.json from pc/protocol.py.

The vectors are checked by BOTH implementations:
  * tests/firmware/test_protocol (C++ codec, `pio test -e native`)
  * tests/protocol/test_protocol_py.py (Python codec)
so any divergence between firmware and PC encoding fails a test.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from pc import protocol as P  # noqa: E402
from pc.protocol import CommandCode, EventCode, MsgType, Packet  # noqa: E402


def vec(name, pkt: Packet, **extra):
    raw = pkt.encode()
    return {
        "name": name,
        "source": pkt.source,
        "destination": pkt.destination,
        "message_type": pkt.message_type,
        "sequence": pkt.sequence,
        "session_id": pkt.session_id,
        "command_id": pkt.command_id,
        "payload_hex": pkt.payload.hex(),
        "hex": raw.hex(),
        "system_id": pkt.magic - P.MAGIC + 1,
        **extra,
    }


def main() -> None:
    S = 0xA1B2C3D4
    v = [
        vec("release_ball", Packet(0x01, 0x20, MsgType.COMMAND, 0x1234, S, 0x01020304,
                                   P.encode_command_payload("RELEASE_BALL"))),
        vec("set_light_mode", Packet(0x01, 0x10, MsgType.COMMAND, 2, S, 1001, P.encode_command_payload(
            "SET_LIGHT_MODE", {"mode": "GAME_PLAY", "brightness": 180, "transition_ms": 500, "speed": 7,
                               "color": [1, 2, 3]}))),
        vec("trigger_effect", Packet(0x01, 0x10, MsgType.COMMAND, 3, S, 1002, P.encode_command_payload(
            "TRIGGER_EFFECT", {"effect": "GAME_CLEAR", "duration_ms": 4000, "intensity": 200}))),
        vec("relay_pulse", Packet(0x01, 0x10, MsgType.COMMAND, 4, S, 1003,
                                  P.encode_command_payload("RELAY_PULSE", {"duration_ms": 1500}))),
        vec("relay_off_all", Packet(0x01, 0x10, MsgType.COMMAND, 5, S, 1004, P.encode_command_payload("RELAY_OFF"))),
        vec("set_mode_game", Packet(0x01, 0x21, MsgType.COMMAND, 6, S, 1005,
                                    P.encode_command_payload("SET_MODE", {"mode": "GAME"}))),
        vec("session_begin", Packet(0x01, 0x21, MsgType.COMMAND, 7, S, 0x80000001,
                                    bytes([CommandCode.SESSION_BEGIN]))),
        vec("ack_accepted_duplicate", Packet(0x20, 0x01, MsgType.ACK, 8, S, 0x01020304,
                                             P.encode_ack_payload(MsgType.COMMAND, 0, 0x01, 0x1234))),
        vec("event_ball_received", Packet(0x20, 0x01, MsgType.EVENT, 9, S, 0x40000001,
                                          P.encode_event_payload(EventCode.BALL_RECEIVED, cycle=3))),
        vec("event_release_done", Packet(0x21, 0x01, MsgType.EVENT, 10, S, 77,
                                         P.encode_event_payload(EventCode.RELEASE_DONE, command_id=1005,
                                                                elapsed_ms=2100))),
        vec("event_relay_done", Packet(0x10, 0x01, MsgType.EVENT, 11, S, 78,
                                       P.encode_event_payload(EventCode.RELAY_DONE, relay_id=0, on_ms=1500, reason=0))),
        vec("set_mist_breath", Packet(0x01, 0x10, MsgType.COMMAND, 15, S, 1006, P.encode_command_payload(
            "SET_MIST", {"pattern": "BREATH", "duration_s": 120}))),
        vec("event_mist_done", Packet(0x10, 0x01, MsgType.EVENT, 16, S, 79,
                                      P.encode_event_payload(EventCode.MIST_DONE, on_s=95, reason=0))),
        vec("system2_set_light_mode", Packet(0x01, 0x10, MsgType.COMMAND, 17, S, 1007, P.encode_command_payload(
            "SET_LIGHT_MODE", {"mode": "AURORA", "brightness": 160}), magic=P.system_magic(2))),
        vec("hub_heartbeat", Packet(0x01, 0x10, MsgType.HEARTBEAT, 12, S, 0, P.encode_hub_heartbeat(123456, 1))),
        vec("node_heartbeat", Packet(0x10, 0x01, MsgType.HEARTBEAT, 13, S, 0,
                                     P.encode_node_heartbeat(12, 0xCAFEBABE, 99999, 2, 0, 0x03, -55, 4, (0, 1)))),
        vec("max_payload", Packet(0x01, 0x20, MsgType.COMMAND, 14, S, 5, bytes(range(P.MAX_PAYLOAD)))),
    ]
    good = Packet(0x01, 0x20, MsgType.COMMAND, 1, S, 1, bytes([CommandCode.STOP_ALL])).encode()
    bad = [
        {"name": "bad_magic", "hex": (b"\x00\x00" + good[2:]).hex(), "error": "bad_magic"},
        {"name": "bad_version", "hex": (good[:2] + b"\x02" + good[3:]).hex(), "error": "bad_version"},
        {"name": "bad_type", "hex": (good[:5] + b"\x09" + good[6:]).hex(), "error": "bad_type"},
        {"name": "length_mismatch", "hex": (good + b"\x00").hex(), "error": "bad_length"},
        {"name": "payload_len_too_big", "hex": (good[:16] + bytes([48]) + good[17:]).hex(), "error": "bad_length"},
        {"name": "too_short", "hex": good[:16].hex(), "error": "too_short"},
        {"name": "too_long", "hex": (good + bytes(60)).hex(), "error": "too_long"},
        # a perfectly valid system 2 frame is rejected by a system 1 decoder
        {"name": "other_system_magic", "hex": Packet(0x01, 0x20, MsgType.COMMAND, 1, S, 1,
                                                     bytes([CommandCode.STOP_ALL]),
                                                     magic=P.system_magic(2)).encode().hex(), "error": "bad_magic"},
    ]
    out = {"comment": "generated by tests/tools/gen_vectors.py - do not edit", "vectors": v + bad}
    path = ROOT / "tests" / "protocol" / "vectors.json"
    path.write_text(json.dumps(out, indent=1) + "\n")
    print(f"wrote {len(out['vectors'])} vectors to {path}")


if __name__ == "__main__":
    main()
