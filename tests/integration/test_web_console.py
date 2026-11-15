"""Browser console (pc/web_console.py): HTTP API over the real DeviceGateway + MockHub."""

import json
import threading
import urllib.error
import urllib.request

import pytest

from pc import DeviceGateway
from pc.mock_hub import MockConfig, MockHub
import socket

from pc.web_console import CSRF_HEADER, DEFAULT_HTTP_PORT, ConsolePortInUse, WebConsole


@pytest.fixture
def console():
    hub = MockHub(MockConfig(system_id=1))
    gw = DeviceGateway(transport=hub, heartbeat_interval=0.1, hub_timeout=0.5, reconnect_interval=0.05, system_id=1)
    assert gw.connect(timeout=2.0)
    c = WebConsole(gw, "127.0.0.1", 0)  # ephemeral port
    c.start()
    yield c
    c.stop()
    gw.disconnect()
    hub.stop()


def req(c, path, body=None, headers=None, host=None):
    h = {"Host": host or f"127.0.0.1:{c.port}"}
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        h.update({"Content-Type": "application/json", CSRF_HEADER: "1"})
    h.update(headers or {})
    r = urllib.request.Request(f"http://127.0.0.1:{c.port}{path}", data=data, headers=h)
    try:
        with urllib.request.urlopen(r, timeout=5) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def test_page_and_meta(console):
    code, body = req(console, "/")
    assert code == 200 and b"UFO Device Console" in body
    code, body = req(console, "/api/meta")
    m = json.loads(body)
    assert m["connected"] and m["hub"]["system_id"] == 1
    assert "MIST_GLOW" in m["enums"]["light_modes"] and "PUFF" in m["enums"]["mist_patterns"]
    assert {d["name"] for d in m["enums"]["devices"]} == {"ILLUMINATION", "M5DIAL_1", "M5DIAL_2"}


def test_command_status_and_devices(console):
    code, body = req(console, "/api/command", {"device": 16, "name": "SET_MIST", "params": {"pattern": "BREATH",
                                                                                          "duration_s": 30}})
    r = json.loads(body)
    assert code == 200 and r["ok"] and r["result"] == "ACCEPTED"
    code, body = req(console, "/api/status?device=16")
    st = json.loads(body)
    assert st["mist_pattern"] == "BREATH" and st["state"] == "MIST_ACTIVE"
    code, body = req(console, "/api/devices")
    assert code == 200 and len(json.loads(body)["devices"]) == 3
    code, body = req(console, "/api/stop_all", {})
    assert code == 200 and all(v["result"] == "ACCEPTED" for v in json.loads(body).values())
    code, body = req(console, "/api/diag")
    assert code == 200


def test_dial_effect_from_console(console):
    code, body = req(console, "/api/meta")
    en = json.loads(body)["enums"]
    assert "CHASE" in en["dial_effects"] and en["dial_directions"] == ["NONE", "TO_SCREEN", "TO_DIAL"]
    assert en["effect_priorities"] == ["BACKGROUND", "NORMAL", "HIGH"] and "BOTH" in en["dial_led_targets"]
    code, body = req(console, "/api/command", {"device": 0x21, "name": "SET_DIAL_EFFECT", "params": {
        "effect": "COMET", "direction": "TO_DIAL", "color": "#ff00ff", "duration_ms": 0}})
    assert code == 200 and json.loads(body)["result"] == "ACCEPTED"
    code, body = req(console, "/api/status?device=33")
    st = json.loads(body)
    assert st["front_effect"] == "COMET" and st["unit_color"] == [255, 0, 255]
    code, body = req(console, "/api/command", {"device": 0x21, "name": "SET_DIAL_EFFECT",
                                               "params": {"effect": "CHASE"}})  # no direction
    assert code == 400 and "direction" in json.loads(body)["error"]


def test_invalid_params_rejected_before_sending(console):
    code, body = req(console, "/api/command", {"device": 16, "name": "SET_MIST", "params": {"pattern": "FOG"}})
    assert code == 400 and "pattern" in json.loads(body)["error"]
    code, _ = req(console, "/api/command", {"device": "16", "name": "STOP_ALL"})
    assert code == 400
    code, _ = req(console, "/api/status?device=x")
    assert code == 400


def test_cross_site_requests_refused(console):
    # no custom header (what a form post from another web page looks like)
    code, _ = req(console, "/api/command", {"device": 16, "name": "STOP_ALL"}, headers={CSRF_HEADER: ""})
    assert code == 403
    # DNS rebinding: a foreign Host header
    code, _ = req(console, "/api/meta", host="evil.example:80")
    assert code == 403
    r = urllib.request.Request(f"http://127.0.0.1:{console.port}/api/command", method="OPTIONS",
                               headers={"Host": f"127.0.0.1:{console.port}"})
    with pytest.raises(urllib.error.HTTPError) as e:
        urllib.request.urlopen(r, timeout=5)
    assert e.value.code == 405 and e.value.headers.get("Access-Control-Allow-Origin") is None


def test_events_stream(console):
    lines = []
    got = threading.Event()

    def reader():
        r = urllib.request.Request(f"http://127.0.0.1:{console.port}/api/events",
                                   headers={"Host": f"127.0.0.1:{console.port}"})
        with urllib.request.urlopen(r, timeout=5) as resp:
            for raw in resp:
                if raw.startswith(b"data: "):
                    ev = json.loads(raw[6:])
                    lines.append(ev)
                    if ev["name"] == "MIST_DONE":
                        got.set()
                        return

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    req(console, "/api/command", {"device": 16, "name": "SET_MIST", "params": {"pattern": "CONTINUOUS"}})
    req(console, "/api/command", {"device": 16, "name": "SET_MIST", "params": {"pattern": "OFF"}})
    assert got.wait(3.0), lines
    done = [e for e in lines if e["name"] == "MIST_DONE"][0]
    assert done["source"] == 16 and done["data"]["reason"] == 1


def test_default_port_avoids_game_backend_and_busy_port_is_reported():
    assert DEFAULT_HTTP_PORT != 8765  # UFO game backend WebSocket
    hub = MockHub(MockConfig())
    gw = DeviceGateway(transport=hub)
    blocker = socket.socket()
    blocker.bind(("127.0.0.1", 0))
    blocker.listen(1)
    try:
        with pytest.raises(ConsolePortInUse, match="already used"):
            WebConsole(gw, "127.0.0.1", blocker.getsockname()[1])
        assert not gw.connected  # the serial side was never touched
    finally:
        blocker.close()
        hub.stop()
