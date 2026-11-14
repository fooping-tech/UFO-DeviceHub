"""Browser test console for the UFO device hub.

    python3 -m pc.web_console --system 1                # serial port auto-detect
    python3 -m pc.web_console --system 1 --port /dev/cu.usbmodem114101
    python3 -m pc.web_console --port mock                # no hardware (pc.mock_hub)

then open http://127.0.0.1:8765/ . Every button goes through the real
DeviceGateway (session, ACK, retries, EVENT_ACK), so the console tests the same
path the game uses. It is a test tool: only one program can own the hub's serial
port, so stop the game (or ufo_cli.py) while the console runs.

Security: the console moves real actuators (mist, servos, relay). It listens on
127.0.0.1 only unless --host is given, refuses requests whose Host header is not
the console itself (DNS rebinding) and requires a custom header on every POST,
so other web pages open in the same browser cannot send commands.

Standard library only (http.server + Server-Sent Events).
"""

from __future__ import annotations

import argparse
import json
import logging
import queue
import threading
import time
import webbrowser
from collections import deque
from dataclasses import asdict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from . import protocol as P
from .device_gateway import DeviceGateway, GatewayError, GatewayNotConnected
from .protocol import CommandResult, DeviceEvent, ProtocolError

log = logging.getLogger("ufo.web")

STATIC = Path(__file__).resolve().parent / "web"
CSRF_HEADER = "X-UFO-Console"
EVENT_HISTORY = 200


def _result_dict(r: CommandResult) -> dict:
    d = asdict(r)
    d.pop("raw", None)
    d["ok"] = r.ok
    return d


def _event_dict(e: DeviceEvent) -> dict:
    d = asdict(e)
    d["time"] = time.strftime("%H:%M:%S", time.localtime()) + f".{int(time.time() * 1000) % 1000:03d}"
    return d


def enums() -> dict:
    """Names the UI offers, taken from pc/protocol.py (single source of truth)."""
    return {
        "light_modes": [m.name for m in P.LightMode],
        "effects": [e.name for e in P.EffectId if e != P.EffectId.NONE],
        "mist_patterns": [m.name for m in P.MistPattern],
        "devices": [{"id": int(d), "name": d.name} for d in P.DeviceId if d != P.DeviceId.HUB],
    }


class WebConsole:
    def __init__(self, gw: DeviceGateway, host: str = "127.0.0.1", port: int = 8765) -> None:
        self.gw = gw
        self._clients: list[queue.Queue] = []
        self._history: deque[dict] = deque(maxlen=EVENT_HISTORY)
        self._lock = threading.Lock()
        self._stopping = threading.Event()
        gw.subscribe(self._on_event)
        console = self

        class Handler(_Handler):
            pass

        Handler.console = console
        self.httpd = ThreadingHTTPServer((host, port), Handler)
        self.httpd.daemon_threads = True
        self.host, self.port = self.httpd.server_address[:2]
        self._thread: threading.Thread | None = None

    @property
    def url(self) -> str:
        return f"http://{self.host}:{self.port}/"

    def allowed_hosts(self) -> set[str]:
        hosts = {f"127.0.0.1:{self.port}", f"localhost:{self.port}", f"[::1]:{self.port}"}
        if self.host not in ("127.0.0.1", "localhost", "::1"):
            hosts.add(f"{self.host}:{self.port}")
        return hosts

    def start(self) -> None:
        self._thread = threading.Thread(target=self.httpd.serve_forever, name="ufo-web", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stopping.set()
        with self._lock:
            for q in self._clients:
                q.put(None)
        self.httpd.shutdown()
        self.httpd.server_close()

    # ---- events -> browsers ---------------------------------------------------
    def _on_event(self, ev: DeviceEvent) -> None:
        d = _event_dict(ev)
        with self._lock:
            self._history.append(d)
            for q in self._clients:
                try:
                    q.put_nowait(d)
                except queue.Full:  # a stalled browser tab must not block the gateway
                    pass

    def add_client(self) -> tuple[queue.Queue, list[dict]]:
        q: queue.Queue = queue.Queue(maxsize=500)
        with self._lock:
            self._clients.append(q)
            return q, list(self._history)

    def remove_client(self, q: queue.Queue) -> None:
        with self._lock:
            if q in self._clients:
                self._clients.remove(q)

    # ---- API ----------------------------------------------------------------
    def meta(self) -> dict:
        return {
            "system_id": self.gw.system_id,
            "connected": self.gw.connected,
            "session_id": self.gw.session_id if self.gw.connected else 0,
            "hub": self.gw.hub_info,
            "enums": enums(),
        }

    def command(self, body: dict) -> dict:
        dev = body.get("device")
        name = body.get("name")
        params = body.get("params") or {}
        if not isinstance(dev, int) or not isinstance(name, str) or not isinstance(params, dict):
            raise ProtocolError("body must be {device: int, name: str, params: object}")
        return _result_dict(self.gw.send_command(dev, name, params))

    def stop_all(self) -> dict:
        return {str(k): _result_dict(v) for k, v in self.gw.stop_all().items()}


class _Handler(BaseHTTPRequestHandler):
    console: WebConsole
    server_version = "UFOConsole/1"

    def log_message(self, fmt: str, *args) -> None:  # quiet: the console has its own log
        log.debug("%s " + fmt, self.address_string(), *args)

    # ---- helpers ---------------------------------------------------------------
    def _send(self, code: int, body: bytes, ctype: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code: int, obj) -> None:
        self._send(code, json.dumps(obj).encode(), "application/json")

    def _host_ok(self) -> bool:
        if self.headers.get("Host", "") in self.console.allowed_hosts():
            return True
        self._json(403, {"error": "bad Host header"})
        return False

    def _guarded(self, fn) -> None:
        try:
            self._json(200, fn())
        except ProtocolError as e:
            self._json(400, {"error": str(e)})
        except GatewayNotConnected as e:
            self._json(503, {"error": f"hub not connected: {e}"})
        except GatewayError as e:
            self._json(504, {"error": str(e)})

    # ---- routes ------------------------------------------------------------------
    def do_GET(self) -> None:
        if not self._host_ok():
            return
        url = urlparse(self.path)
        c = self.console
        if url.path in ("/", "/index.html"):
            self._send(200, (STATIC / "index.html").read_bytes(), "text/html; charset=utf-8")
        elif url.path == "/api/meta":
            self._json(200, c.meta())
        elif url.path == "/api/devices":
            self._guarded(lambda: {"devices": c.gw.get_devices()})
        elif url.path == "/api/diag":
            self._guarded(c.gw.get_diagnostics)
        elif url.path == "/api/status":
            q = parse_qs(url.query)
            try:
                dev = int(q.get("device", [""])[0], 0)
            except ValueError:
                self._json(400, {"error": "device=<id> required"})
                return
            self._guarded(lambda: c.gw.get_status(dev))
        elif url.path == "/api/events":
            self._events()
        else:
            self._json(404, {"error": "not found"})

    def do_POST(self) -> None:
        if not self._host_ok():
            return
        # A custom header + JSON body force a CORS preflight, which this server
        # never answers: other web pages cannot POST here.
        if self.headers.get(CSRF_HEADER) != "1" or not self.headers.get("Content-Type", "").startswith(
            "application/json"
        ):
            self._json(403, {"error": f"{CSRF_HEADER} header and JSON body required"})
            return
        try:
            n = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(n) or b"{}") if 0 <= n <= 4096 else None
        except (ValueError, json.JSONDecodeError):
            body = None
        if not isinstance(body, dict):
            self._json(400, {"error": "bad JSON body"})
            return
        path = urlparse(self.path).path
        if path == "/api/command":
            self._guarded(lambda: self.console.command(body))
        elif path == "/api/stop_all":
            self._guarded(self.console.stop_all)
        else:
            self._json(404, {"error": "not found"})

    def do_OPTIONS(self) -> None:  # no CORS, ever
        self._json(405, {"error": "cross-origin requests are not allowed"})

    def _events(self) -> None:
        c = self.console
        q, history = c.add_client()
        try:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            for d in history:
                self.wfile.write(f"data: {json.dumps(d)}\n\n".encode())
            self.wfile.flush()
            while not c._stopping.is_set():
                try:
                    d = q.get(timeout=10)
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")  # keep-alive, detects closed tabs
                    self.wfile.flush()
                    continue
                if d is None:
                    break
                self.wfile.write(f"data: {json.dumps(d)}\n\n".encode())
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            c.remove_client(q)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="auto", help="hub serial port, 'auto' or 'mock'")
    ap.add_argument("--system", type=int, default=None, help="UFO system id (1, 2): attach only to that hub")
    ap.add_argument("--host", default="127.0.0.1", help="listen address (default: this PC only)")
    ap.add_argument("--http-port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO, format="%(asctime)s %(message)s")

    if a.port == "mock":
        from .mock_hub import MockConfig, MockHub

        gw = DeviceGateway(transport=MockHub(MockConfig(system_id=a.system or 1)), system_id=a.system)
    else:
        gw = DeviceGateway(port=a.port, system_id=a.system)
    if a.host not in ("127.0.0.1", "localhost", "::1"):
        log.warning("listening on %s: anyone on this network can move the actuators", a.host)
    console = WebConsole(gw, a.host, a.http_port)
    gw.connect(wait=False)  # the page shows the connection state; reconnects automatically
    console.start()
    log.info("UFO web console: %s", console.url)
    if not a.no_browser:
        webbrowser.open(console.url)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        console.stop()
        gw.disconnect()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
