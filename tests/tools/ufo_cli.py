"""Manual operation / on-site diagnostics for the UFO device hub.

    python3 tests/tools/ufo_cli.py [--port auto|/dev/cu.usbserial-XXXX|mock] <command>

Commands
  devices                       list devices (online, rtt, session)
  diag                          hub diagnostics (counters, invalid packets, ...)
  status <dev>                  STATUS of one device (illum | dial1 | dial2 | 0x20 ...)
  light <MODE> [brightness]     SET_LIGHT_MODE
  effect <EFFECT> [ms]          TRIGGER_EFFECT
  steam <ms>                    RELAY_PULSE (needs a configured relay pin)
  game <dev>                    SET_MODE GAME + ARM_RECEIVER
  release <dev>                 RELEASE_BALL (waits for RELEASE_DONE)
  stop                          STOP_ALL on every device
  monitor                       print every event until Ctrl-C
  probe [seconds] [dev]         latency / loss measurement with GET_STATUS commands

`--port mock` runs against pc.mock_hub (no hardware).
"""

import argparse
import json
import logging
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from pc import DeviceGateway, DeviceId  # noqa: E402

ALIASES = {"illum": DeviceId.ILLUMINATION, "illumination": DeviceId.ILLUMINATION,
           "dial1": DeviceId.M5DIAL_1, "dial2": DeviceId.M5DIAL_2}


def dev(s: str) -> int:
    return int(ALIASES.get(s.lower(), None) or int(s, 0))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="auto")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO, format="%(asctime)s %(message)s")

    if a.port == "mock":
        from pc.mock_hub import MockHub
        gw = DeviceGateway(transport=MockHub())
    else:
        gw = DeviceGateway(port=a.port)
    events = gw.event_queue()
    if not gw.connect(timeout=5.0):
        print("hub not reachable", file=sys.stderr)
        return 2
    time.sleep(1.5)  # let the hub sync the session to the devices
    try:
        c, args = a.cmd, a.args
        if c == "devices":
            for d in gw.get_devices():
                print(json.dumps(d))
        elif c == "diag":
            print(json.dumps(gw.get_diagnostics(), indent=1))
        elif c == "status":
            print(json.dumps(gw.get_status(dev(args[0])), indent=1))
        elif c == "light":
            print(gw.set_light_mode(args[0], brightness=int(args[1]) if len(args) > 1 else 128))
        elif c == "effect":
            print(gw.trigger_effect(args[0], duration_ms=int(args[1]) if len(args) > 1 else 0))
        elif c == "steam":
            print(gw.relay_pulse(int(args[0])))
        elif c == "game":
            d = dev(args[0])
            print(gw.set_mode(d, "GAME"))
            print(gw.arm_receiver(d))
            print("waiting for BALL_RECEIVED (Ctrl-C to stop) ...")
            while True:
                print(events.get())
        elif c == "release":
            r = gw.release_ball(dev(args[0]))
            print(r)
            end = time.time() + 5
            while r.ok and time.time() < end:
                ev = events.get(timeout=5)
                print(ev)
                if ev.name in ("RELEASE_DONE", "RELEASE_FAILED"):
                    break
        elif c == "stop":
            for d, r in gw.stop_all().items():
                print(hex(d), r.result)
        elif c == "monitor":
            while True:
                print(events.get())
        elif c == "probe":
            seconds = float(args[0]) if args else 30
            targets = [dev(args[1])] if len(args) > 1 else [DeviceId.ILLUMINATION, DeviceId.M5DIAL_1, DeviceId.M5DIAL_2]
            stats = {t: {"rtt": [], "fail": {}, "retries": 0} for t in targets}
            end = time.time() + seconds
            while time.time() < end:
                for t in targets:
                    t0 = time.perf_counter()
                    r = gw.send_command(t, "GET_STATUS")
                    ms = (time.perf_counter() - t0) * 1000
                    s = stats[t]
                    if r.ok:
                        s["rtt"].append(ms)
                        s["retries"] += max(0, r.attempts - 1)
                    else:
                        s["fail"][r.result] = s["fail"].get(r.result, 0) + 1
                time.sleep(0.05)
            for t, s in stats.items():
                n_ok = len(s["rtt"])
                n = n_ok + sum(s["fail"].values())
                if n_ok:
                    q = statistics.quantiles(s["rtt"], n=20) if n_ok >= 20 else [max(s["rtt"])] * 19
                    print(f"{t:#04x}: {n} cmds  ok={n_ok}  fail={s['fail']}  retransmissions={s['retries']}  "
                          f"rtt median={statistics.median(s['rtt']):.1f}ms p95={q[18]:.1f}ms max={max(s['rtt']):.1f}ms")
                else:
                    print(f"{t:#04x}: {n} cmds, all failed {s['fail']}")
            print(json.dumps(gw.get_diagnostics().get("devices", []), indent=1))
        else:
            ap.print_help()
            return 1
    except KeyboardInterrupt:
        pass
    finally:
        gw.disconnect()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
