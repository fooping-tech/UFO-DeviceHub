"""DeviceGateway (real code) against MockHub (simulated hub + devices)."""

import asyncio
import dataclasses
import json
import queue
import time

import pytest

from pc import DeviceGateway, DeviceId, GatewayNotConnected, ProtocolError
from pc import protocol as P
from pc.mock_hub import MockConfig, MockDial, MockHub

D1, D2, ILL = DeviceId.M5DIAL_1, DeviceId.M5DIAL_2, DeviceId.ILLUMINATION
REMOTE = DeviceId.STAFF_REMOTE

FAST = MockConfig(
    servo_hold_ms=150, servo_settle_ms=50, relay_cooldown_ms=200, pc_timeout=0.6, pc_event_retry=0.1
)


def wait_for(pred, timeout=2.0, step=0.01):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if pred():
            return True
        time.sleep(step)
    return False


class Collector:
    def __init__(self, gw):
        self.events = []
        gw.subscribe(self.events.append)

    def names(self, name):
        return [e for e in self.events if e.name == name]

    def wait(self, name, n=1, timeout=2.0):
        assert wait_for(lambda: len(self.names(name)) >= n, timeout), f"{name} x{n} not received: {self.events}"
        return self.names(name)


@pytest.fixture
def rig():
    hub = MockHub(dataclasses.replace(FAST))  # own copy: a test may change the configuration
    gw = DeviceGateway(transport=hub, heartbeat_interval=0.1, hub_timeout=0.5, reconnect_interval=0.05,
                       command_timeout=1.0)
    col = Collector(gw)
    assert gw.connect(timeout=2.0)
    yield hub, gw, col
    gw.disconnect()
    hub.stop()


def game_ready(gw, dev=D1):
    assert gw.set_mode(dev, "GAME").ok
    assert gw.arm_receiver(dev).ok


def test_connect_establishes_session(rig):
    hub, gw, col = rig
    assert gw.connected and gw.session_id == hub.session != 0
    assert hub.received[0]["type"] == "HELLO"
    col.wait("GATEWAY_CONNECTED")
    devs = {d["id"]: d for d in gw.get_devices()}
    assert set(devs) == {ILL, D1, D2, REMOTE} and all(d["online"] for d in devs.values())
    assert gw.get_diagnostics()["type"] == "DIAG"


def test_full_ball_cycle(rig):
    hub, gw, col = rig
    assert gw.set_light_mode("GAME_PLAY", brightness=180, transition_ms=300).ok
    game_ready(gw)
    assert hub.ball(D1)
    ev = col.wait("BALL_RECEIVED")[0]
    assert ev.source == D1 and ev.data["cycle"] == 1 and ev.event_id
    res = gw.release_ball(D1)
    assert res.ok and res.origin == "device"
    started = col.wait("RELEASE_STARTED")[0]
    done = col.wait("RELEASE_DONE")[0]
    assert started.data["command_id"] == done.data["command_id"] == res.command_id
    assert hub.devices[D1].servo_moves == 1
    assert hub.devices[D1].dial_state == "COMPLETE"
    # the other dial was never touched
    assert hub.devices[D2].dial_state == "DISABLED"


def test_release_rules(rig):
    hub, gw, col = rig
    assert gw.release_ball(D1).result == "REJECTED_STATE"  # not in GAME, no ball
    game_ready(gw)
    assert gw.release_ball(D1).result == "REJECTED_STATE"  # armed but no ball yet
    hub.ball(D1)
    col.wait("BALL_RECEIVED")
    assert gw.release_ball(D1).ok
    assert gw.release_ball(D1).result == "BUSY"  # during the servo sequence
    col.wait("RELEASE_DONE")
    assert gw.release_ball(D1).result == "REJECTED_STATE"  # needs a new cycle
    assert hub.devices[D1].servo_moves == 1


def test_release_again_while_the_ball_stays(rig):
    """0.4: after RELEASE_DONE the ball is still on the switch: a second RELEASE_BALL is
    accepted (the PC decides to try again); once it left, a release is refused as before."""
    hub, gw, col = rig
    game_ready(gw)
    hub.devices[D1].stuck_releases = 1
    hub.ball(D1)
    col.wait("BALL_RECEIVED")
    assert gw.release_ball(D1).ok
    col.wait("RELEASE_DONE")
    st = hub.devices[D1].status()
    assert st["dial_state"] == "COMPLETE" and st["switch_on"]
    assert gw.release_ball(D1).ok  # still there: once more
    deadline = time.time() + 5
    while hub.devices[D1].busy and time.time() < deadline:
        time.sleep(0.02)
    st = hub.devices[D1].status()
    assert st["dial_state"] == "COMPLETE" and not st["switch_on"] and hub.devices[D1].servo_moves == 2
    assert gw.release_ball(D1).result == "REJECTED_STATE"  # it left: a new cycle is needed


def test_ack_loss_recovered_without_double_execution(rig):
    hub, gw, col = rig
    game_ready(gw)
    hub.ball(D1)
    col.wait("BALL_RECEIVED")
    hub.lose_acks(D1, 2)
    res = gw.release_ball(D1)
    assert res.ok and res.duplicate and res.attempts == 3
    assert hub.devices[D1].servo_moves == 1


def test_ack_loss_beyond_retries_is_timeout(rig):
    hub, gw, col = rig
    hub.lose_acks(ILL, 9)
    res = gw.trigger_effect("SCORE")
    assert res.result == "TIMEOUT" and not res.ok


def test_duplicate_and_resent_events_delivered_once(rig):
    hub, gw, col = rig
    hub.duplicate_events = True
    hub.ignore_event_acks = True  # hub keeps resending every 100 ms
    game_ready(gw)
    hub.ball(D1)
    col.wait("BALL_RECEIVED")
    time.sleep(0.5)
    assert len(col.names("BALL_RECEIVED")) == 1
    assert gw.stats["duplicate_events"] >= 3
    acks = [m for m in hub.received if m["type"] == "EVENT_ACK"]
    assert len(acks) >= 4  # every copy is acknowledged


def test_event_ack_stops_resend(rig):
    hub, gw, col = rig
    game_ready(gw)
    hub.ball(D1)
    col.wait("BALL_RECEIVED")
    time.sleep(0.4)
    assert hub.counters["event_resends"] == 0


def test_stale_event_from_old_session_dropped(rig):
    hub, gw, col = rig
    hub._out.put(json.dumps({"type": "EVENT", "source": int(D1), "session_id": 12345, "event_id": 1,
                             "name": "BALL_RECEIVED", "data": {"cycle": 1}}))
    assert wait_for(lambda: gw.stats["stale_events"] == 1)
    assert not col.names("BALL_RECEIVED")
    assert any(m["type"] == "EVENT_ACK" and m["event_id"] == 1 for m in hub.received)


def test_relay_pulse_limits(rig):
    hub, gw, col = rig
    r = gw.relay_pulse(5000)
    assert r.ok and r.clamped  # limited to 3000 ms on the device
    assert gw.relay_pulse(100).result == "BUSY"
    assert gw.relay_off().ok
    done = col.wait("RELAY_DONE")[0]
    assert done.data["reason"] == 1  # OFF_COMMAND
    assert gw.relay_pulse(100).result == "COOLDOWN"
    time.sleep(0.25)
    assert gw.relay_pulse(100).ok
    assert col.wait("RELAY_DONE", 2)[1].data["reason"] == 0  # COMPLETED
    with pytest.raises(ProtocolError):
        gw.relay_pulse(0)


def test_light_mode_and_effect_priority(rig):
    hub, gw, col = rig
    assert gw.set_light_mode("CUSTOM", color="#FF8800", speed=0).ok
    assert gw.trigger_effect("SCORE", duration_ms=1000).ok
    assert gw.trigger_effect("ARM_MOVE").result == "IGNORED_PRIORITY"
    assert gw.trigger_effect("GAME_CLEAR").ok
    assert gw.set_light_mode("ERROR").ok
    assert gw.trigger_effect("GAME_CLEAR").result == "IGNORED_PRIORITY"
    st = gw.get_status(ILL)
    assert st["light_mode"] == "ERROR" and st["device_type"] == "ILLUMINATION"


def test_device_offline_and_recovery(rig):
    hub, gw, col = rig
    hub.set_device_online(D2, False)
    col.wait("DEVICE_OFFLINE")
    assert gw.set_mode(D2, "GAME").result == "OFFLINE"
    hub.set_device_online(D2, True, reboot=True)
    col.wait("DEVICE_REBOOTED")
    col.wait("DEVICE_ONLINE")
    assert gw.set_mode(D2, "GAME").ok


def test_usb_unplug_safe_state_and_resync(rig):
    hub, gw, col = rig
    old_session = gw.session_id
    game_ready(gw)
    assert gw.relay_pulse(3000).ok
    hub.unplug()
    col.wait("GATEWAY_DISCONNECTED")
    assert not gw.connected
    with pytest.raises(GatewayNotConnected):
        gw.release_ball(D1)  # never queued for later
    # hub side: PC timeout -> session closed -> devices safe
    assert wait_for(lambda: hub.session == 0, 2.0)
    assert not hub.devices[ILL].relay_on
    assert hub.devices[D1].dial_state == "DISABLED"
    hub.replug()
    col.wait("GATEWAY_CONNECTED", 2, timeout=3.0)
    assert gw.connected and gw.session_id != old_session == old_session
    assert hub.session == gw.session_id
    # previous game state does not carry over
    assert gw.release_ball(D1).result == "REJECTED_STATE"


def test_hub_silent_triggers_reconnect(rig):
    hub, gw, col = rig
    hub.silent = True
    col.wait("GATEWAY_DISCONNECTED", timeout=2.0)
    hub.silent = False
    col.wait("GATEWAY_CONNECTED", 2, timeout=3.0)
    assert gw.connected


def test_hub_reboot_resynchronises_session(rig):
    hub, gw, col = rig
    old = gw.session_id
    hub.reboot_hub()
    assert wait_for(lambda: gw.session_id != old and hub.session == gw.session_id and gw.connected)
    assert gw.set_light_mode("IDLE").ok


def test_session_closed_by_hub_is_reestablished(rig):
    hub, gw, col = rig
    old = gw.session_id
    with hub._lock:
        hub._close_session("pc_timeout")
    col.wait("SESSION_CLOSED")
    assert wait_for(lambda: gw.connected and gw.session_id != old)


def test_invalid_command_never_sent(rig):
    hub, gw, col = rig
    n = len(hub.received)
    with pytest.raises(ProtocolError):
        gw.set_light_mode("DISCO")
    with pytest.raises(ProtocolError):
        gw.send_command(D1, "SESSION_BEGIN")
    assert all(m["type"] in ("HEARTBEAT",) for m in hub.received[n:])


def test_stop_all(rig):
    hub, gw, col = rig
    gw.relay_pulse(2000)
    res = gw.stop_all()
    assert set(res) == {ILL, D1, D2, REMOTE} and all(r.ok for r in res.values())
    assert not hub.devices[ILL].relay_on


def test_event_queue_and_asyncio(rig):
    hub, gw, col = rig
    q = gw.event_queue({"BALL_RECEIVED"})
    loop = asyncio.new_event_loop()
    aq = gw.asyncio_queue(loop, {"BALL_RECEIVED"})
    game_ready(gw, D2)
    hub.ball(D2)
    assert q.get(timeout=2.0).source == D2

    async def get():
        return await asyncio.wait_for(aq.get(), 2.0)

    assert loop.run_until_complete(get()).source == D2
    loop.close()


def test_subscriber_exception_does_not_break_dispatch(rig):
    hub, gw, col = rig

    def boom(ev):
        raise RuntimeError("bad subscriber")

    gw.subscribe(boom)
    game_ready(gw)
    hub.ball(D1)
    col.wait("BALL_RECEIVED")


def test_dial_menu_request_event(rig):
    """GAME RESTART / GAME END chosen in the menu on a dial arrive as DIAL_REQUEST events."""
    hub, gw, col = rig
    assert hub.dial_request(D2, "GAME_RESTART")
    ev = col.wait("DIAL_REQUEST")[0]
    assert ev.source == D2 and ev.data["request"] == "GAME_RESTART"
    assert P.DialRequest[ev.data["request"]] == P.DialRequest.GAME_RESTART == ev.data["code"]
    assert hub.dial_request(D1, "GAME_END")
    ev = col.wait("DIAL_REQUEST", 2)[1]
    assert ev.source == D1 and ev.data["request"] == "GAME_END"
    assert not hub.dial_request(ILL, "GAME_END")  # only the dials have the menu
    hub.set_device_online(D1, False)
    assert not hub.dial_request(D1, "GAME_END")
    with pytest.raises(KeyError):
        hub.dial_request(D2, "GAME_PAUSE")


def test_staff_remote_actions(rig):
    """The staff remote's STAFF_ACTION events: one per press, the action by name and code."""
    hub, gw, col = rig
    for i, a in enumerate(["NEXT", "RECOVER", "STOP"], start=1):
        assert hub.staff_action(a)
        ev = col.wait("STAFF_ACTION", i)[i - 1]
        assert ev.source == REMOTE and ev.data["action"] == a and ev.data["code"] == int(P.StaffAction[a])
    with pytest.raises(KeyError):
        hub.staff_action("JUMP")
    hub.set_device_online(REMOTE, False)
    assert not hub.staff_action("NEXT")


def test_staff_remote_input_stream(rig):
    """REMOTE_INPUT frames: the move key, the tilt, the shape, an increasing frame number."""
    hub, gw, col = rig
    assert hub.remote_input(move=True, x=40, y=-100)
    assert hub.remote_input(move=True, z=True, y=55)
    assert hub.remote_input(pose="FIST")
    assert hub.remote_input()  # the closing frame: nothing held
    evs = col.wait("REMOTE_INPUT", 4)
    d = [e.data for e in evs]
    assert d[0] == {"move": True, "z": False, "x": 40, "y": -100, "pose": "OPEN", "frame": 1}
    assert d[1]["z"] and d[1]["y"] == 55 and d[2]["pose"] == "FIST" and not d[2]["move"]
    assert d[3] == {"move": False, "z": False, "x": 0, "y": 0, "pose": "OPEN", "frame": 4}
    with pytest.raises(ValueError):
        hub.remote_input(x=101)
    # the remote takes STOP_ALL (ends its stream), nothing else
    assert gw.send_command(REMOTE, "STOP_ALL", {}).ok
    assert gw.send_command(REMOTE, "SET_MODE", {"mode": "GAME"}).result == "UNSUPPORTED"


def test_ball_ignored_outside_game(rig):
    hub, gw, col = rig
    assert not hub.ball(D1)
    assert gw.set_mode(D1, "GAME").ok
    assert not hub.ball(D1)  # READY but not armed
    time.sleep(0.1)
    assert not col.names("BALL_RECEIVED")
    assert isinstance(hub.devices[D1], MockDial)


def test_not_connected_before_connect():
    hub = MockHub(FAST)
    gw = DeviceGateway(transport=hub)
    with pytest.raises(GatewayNotConnected):
        gw.arm_receiver(D1)
    hub.stop()


def test_reconnect_when_port_appears_later():
    hub = MockHub(FAST)
    hub.unplug()
    gw = DeviceGateway(transport=hub, heartbeat_interval=0.1, hub_timeout=0.5, reconnect_interval=0.05)
    assert not gw.connect(timeout=0.3)
    hub.replug()
    assert gw.wait_ready(2.0)
    gw.disconnect()
    hub.stop()
    assert queue  # keep import used


def test_mist_pattern_status_and_done_event(rig):
    hub, gw, col = rig
    r = gw.set_mist("BREATH", duration_s=1)
    assert r.ok and not r.clamped
    st = gw.get_status(ILL)
    assert st["mist_pattern"] == "BREATH" and st["mist_on"] and st["state"] == "MIST_ACTIVE"
    ev = col.wait("MIST_DONE", timeout=3.0)[0]
    assert ev.data["reason"] == 0  # COMPLETED
    assert gw.set_mist("CONTINUOUS", duration_s=9999).clamped
    assert gw.mist_off().ok
    assert col.wait("MIST_DONE", n=2)[1].data["reason"] == 1  # OFF_COMMAND
    assert gw.set_light_mode("AURORA").ok
    with pytest.raises(ProtocolError):
        gw.set_mist("FOG")


def test_gateway_refuses_hub_of_another_system():
    hub = MockHub(MockConfig(system_id=2, channel=11))
    gw = DeviceGateway(transport=hub, heartbeat_interval=0.1, hub_timeout=0.5, reconnect_interval=0.05,
                       system_id=1)
    try:
        assert not gw.connect(timeout=0.8)  # never attaches to system 2's hub
        assert hub.session == 0
    finally:
        gw.disconnect()
        hub.stop()
    hub = MockHub(MockConfig(system_id=2, channel=11))
    gw = DeviceGateway(transport=hub, heartbeat_interval=0.1, hub_timeout=0.5, reconnect_interval=0.05,
                       system_id=2)
    try:
        assert gw.connect(timeout=2.0)
        assert gw.hub_info["system_id"] == 2 and gw.hub_info["channel"] == 11
    finally:
        gw.disconnect()
        hub.stop()


class _ChattyThenHub:
    """Auto-detect: the first port is a node printing its debug log (not a hub),
    the next one is the hub."""

    def __init__(self, hub):
        self.hub = hub
        self.on_hub = False
        self.rejected = 0

    def reject_current(self):
        self.rejected += 1
        self.on_hub = True

    def open(self):
        if self.on_hub:
            self.hub.open()

    def close(self):
        if self.on_hub:
            self.hub.close()

    def write_line(self, line):
        if self.on_hub:
            self.hub.write_line(line)

    def read_line(self, timeout):
        if self.on_hub:
            return self.hub.read_line(timeout)
        import time as _t

        _t.sleep(min(timeout, 0.01))
        return "[illum] led frame ok"  # plain log text, never a protocol line


def test_auto_detect_skips_port_that_is_not_a_hub():
    hub = MockHub(MockConfig(system_id=1))
    t = _ChattyThenHub(hub)
    gw = DeviceGateway(transport=t, heartbeat_interval=0.1, hub_timeout=0.4, reconnect_interval=0.05, system_id=1)
    try:
        assert gw.connect(timeout=3.0)
        assert t.rejected == 1 and gw.hub_info["system_id"] == 1
    finally:
        gw.disconnect()
        hub.stop()


# ---- game mode: dial LED effects (docs/game-mode-interface.md) ----------------
def test_dial_effect_basic_and_status(rig):
    hub, gw, col = rig
    res = gw.set_dial_effect(D1, "CHASE", "TO_SCREEN", duration_ms=0)
    assert res.ok and res.origin == "device"
    led = hub.devices[D1].led("FRONT")
    assert (led["source"], led["effect"], led["direction"]) == ("pc", "CHASE", "TO_SCREEN")
    assert led["color"] == (0, 255, 255)  # no colour given: unit colour of dial 1 (cyan)
    st = gw.get_status(D1)
    assert st["front_effect"] == "CHASE" and st["transfer_effect"] == "CHASE"  # BOTH: front (G1) + G15 LED
    assert st["transfer_led"] is True and st["unit_color"] == [0, 255, 255]
    assert hub.devices[D1].led("TRANSFER")["direction"] == "TO_SCREEN"
    assert hub.devices[D2].led("FRONT")["source"] == "local"  # the other dial is not touched
    assert gw.clear_dial_effect(D1).ok
    assert hub.devices[D1].led("FRONT")["source"] == "local"
    assert gw.set_dial_effect(ILL, "SOLID").result == "UNSUPPORTED"  # not a dial


def test_dial_effect_invalid_params_never_sent(rig):
    hub, gw, col = rig
    n = len(hub.received)
    for bad in ({"effect": "CHASE"}, {"effect": "SOLID", "priority": "URGENT"}, {"effect": "DISCO"}):
        with pytest.raises(ProtocolError):
            gw.send_command(D1, "SET_DIAL_EFFECT", bad)
    assert not [m for m in hub.received[n:] if m["type"] == "COMMAND"]


def test_dial_effect_priority_and_expiry(rig):
    hub, gw, col = rig
    assert gw.set_dial_effect(D1, "FLASH", color=(255, 0, 0), duration_ms=250, priority="HIGH").ok
    assert gw.set_dial_effect(D1, "SOLID", priority="NORMAL").result == "IGNORED_PRIORITY"
    assert gw.clear_dial_effect(D1, priority="NORMAL").result == "IGNORED_PRIORITY"
    assert hub.devices[D1].led("FRONT")["effect"] == "FLASH"
    assert gw.set_dial_effect(D1, "SPARKLE", priority="HIGH").ok  # equal priority replaces
    assert gw.set_dial_effect(D1, "FLASH", duration_ms=150, priority="HIGH").ok
    assert wait_for(lambda: hub.devices[D1].led("FRONT")["source"] == "local", 1.0)  # duration over
    assert gw.set_dial_effect(D1, "SOLID", priority="BACKGROUND").ok


def test_dial_transfer_strip_not_fitted_and_fitted(rig):
    hub, gw, col = rig
    hub.cfg.dial_transfer_led = False  # a unit built without the G15 LED (DIAL_TRANSFER_LED_PIN -1)
    assert gw.set_dial_effect(D1, "CHASE", "TO_DIAL", target="TRANSFER").result == "NOT_CONFIGURED"
    assert gw.set_dial_effect(D1, "CHASE", "TO_DIAL", target="BOTH").ok  # runs on the front LED only
    assert hub.devices[D1].led("TRANSFER")["scene"] == "OFF"
    hub.cfg.dial_transfer_led = True  # the real machine: second LED on G15
    assert gw.set_dial_effect(D1, "COMET", "TO_DIAL", target="TRANSFER").ok
    assert hub.devices[D1].led("TRANSFER")["effect"] == "COMET"
    assert hub.devices[D1].led("FRONT")["effect"] == "CHASE"  # outputs are independent
    st = gw.get_status(D1)
    assert st["transfer_led"] is True and st["transfer_effect"] == "COMET" and st["front_effect"] == "CHASE"


def test_local_receive_animation_wins_then_pc_effect(rig):
    hub, gw, col = rig
    game_ready(gw)
    assert gw.set_dial_effect(D1, "BREATHE", priority="HIGH").ok  # attract while waiting
    hub.ball(D1)
    dial = hub.devices[D1]
    t0 = dial.detected_at
    flash = dial.led("FRONT", now=t0 + 10)
    assert (flash["source"], flash["effect"], flash["color"]) == ("local", "SOLID", (255, 255, 255))
    chase = dial.led("FRONT", now=t0 + 400)  # the PC effect was dropped: local chase in the unit colour
    assert (chase["source"], chase["effect"], chase["direction"]) == ("local", "CHASE", "TO_SCREEN")
    assert dial.led("FRONT", now=t0 + 5000)["effect"] == "BREATHE"  # transfer shown: holding
    col.wait("BALL_RECEIVED")
    assert gw.set_dial_effect(D1, "CHASE", "TO_SCREEN", duration_ms=1500).ok  # the late PC effect is consistent
    assert wait_for(lambda: dial.led("FRONT")["source"] == "pc", 1.0)  # right after the 150 ms white flash
    led = dial.led("FRONT")
    assert (led["direction"], led["color"]) == ("TO_SCREEN", (0, 255, 255))


def test_left_right_simultaneous_receive_and_release(rig):
    hub, gw, col = rig
    for d in (D1, D2):
        game_ready(gw, d)
    assert hub.ball(D1) and hub.ball(D2)  # both capsules at the same time
    evs = col.wait("BALL_RECEIVED", 2)
    assert {e.source for e in evs} == {D1, D2}
    for d, colour in ((D1, (0, 255, 255)), (D2, (255, 0, 255))):
        led = hub.devices[d].led("FRONT", now=hub.devices[d].detected_at + 400)
        assert (led["direction"], led["color"]) == ("TO_SCREEN", colour)
    futs = {d: gw.release_ball(d, wait=False) for d in (D1, D2)}  # both released together
    ids = {d: f.result(timeout=1.0) for d, f in futs.items()}
    assert all(r.ok for r in ids.values())
    for d in (D1, D2):
        assert hub.devices[d].led("FRONT")["direction"] == "TO_DIAL"
    done = col.wait("RELEASE_DONE", 2)
    assert {(e.source, e.data["command_id"]) for e in done} == {(d, r.command_id) for d, r in ids.items()}
    assert hub.devices[D1].servo_moves == hub.devices[D2].servo_moves == 1


def test_left_right_opposite_directions(rig):
    hub, gw, col = rig
    for d in (D1, D2):
        game_ready(gw, d)
    hub.ball(D2)  # the right capsule is already in the game ...
    col.wait("BALL_RECEIVED")
    assert gw.set_dial_effect(D2, "COMET", "TO_DIAL").ok  # ... and returns to the right dial
    hub.ball(D1)  # while the left dial takes a new capsule in
    col.wait("BALL_RECEIVED", 2)
    assert gw.set_dial_effect(D1, "CHASE", "TO_SCREEN", duration_ms=1500).ok
    assert wait_for(lambda: hub.devices[D1].led("FRONT")["source"] == "pc", 1.0)  # after the white flash
    left, right = hub.devices[D1].led("FRONT"), hub.devices[D2].led("FRONT")
    assert (left["direction"], left["color"]) == ("TO_SCREEN", (0, 255, 255))
    assert (right["direction"], right["color"]) == ("TO_DIAL", (255, 0, 255))
    res = gw.release_ball(D2)
    assert res.ok
    col.wait("RELEASE_DONE")
    assert hub.devices[D2].led("FRONT")["source"] == "local"  # the dial ends the release animation itself
    assert hub.devices[D1].led("FRONT")["direction"] == "TO_SCREEN"  # the left side was not disturbed
    assert hub.devices[D1].dial_state == "BALL_DETECTED" and hub.devices[D1].servo_moves == 0


def test_effect_command_resend_is_not_reapplied(rig):
    hub, gw, col = rig
    hub.lose_acks(D1, 2)
    res = gw.set_dial_effect(D1, "WIPE", "TO_DIAL", duration_ms=800)
    assert res.ok and res.duplicate and res.attempts == 3
    assert len(hub.devices[D1].effect_log) == 1  # applied once: the animation did not restart


def test_effects_cleared_on_stop_session_and_link_loss(rig):
    hub, gw, col = rig
    assert gw.set_dial_color(D1, "#102030").ok
    assert gw.set_dial_effect(D1, "SOLID", priority="HIGH").ok
    assert hub.devices[D1].led("FRONT")["color"] == (16, 32, 48)
    gw.stop_all()
    assert hub.devices[D1].led("FRONT")["source"] == "local"
    assert gw.set_dial_effect(D2, "SOLID", priority="HIGH").ok
    hub.set_device_online(D2, False)  # link lost: nobody could stop the effect any more
    assert hub.devices[D2].slots["FRONT"] is None
    assert gw.set_dial_effect(D2, "SOLID").result == "OFFLINE"
    assert gw.set_dial_effect(D1, "SOLID", priority="HIGH").ok
    hub.unplug()  # PC gone -> session closed -> effects and session colour dropped
    assert wait_for(lambda: hub.devices[D1].session == 0, 2.0)
    assert hub.devices[D1].slots["FRONT"] is None and hub.devices[D1].color == (0, 255, 255)


def test_stop_all_during_release_ends_effect_and_servo_once(rig):
    hub, gw, col = rig
    game_ready(gw)
    hub.ball(D1)
    col.wait("BALL_RECEIVED")
    assert gw.set_dial_effect(D1, "COMET", "TO_DIAL").ok
    assert gw.release_ball(D1).ok
    gw.stop_all()
    col.wait("RELEASE_FAILED")
    assert hub.devices[D1].slots["FRONT"] is None and hub.devices[D1].servo_moves == 1
    assert hub.devices[D1].dial_state == "DISABLED"


def test_led_fault_never_blocks_release(rig):
    hub, gw, col = rig
    game_ready(gw)
    hub.ball(D1)
    col.wait("BALL_RECEIVED")
    # every LED command fails (LED missing / lower priority): the release still runs
    hub.cfg.dial_transfer_led = False
    assert gw.set_dial_effect(D1, "SOLID", priority="HIGH").ok
    assert gw.set_dial_effect(D1, "CHASE", "TO_DIAL", target="TRANSFER").result == "NOT_CONFIGURED"
    assert gw.set_dial_effect(D1, "CHASE", "TO_DIAL").result == "IGNORED_PRIORITY"
    assert gw.release_ball(D1).ok
    col.wait("RELEASE_DONE")


def test_game_mode_presets_and_trace(tmp_path):
    from pc import TraceLog
    from pc import game_mode as gm

    hub = MockHub(FAST)
    trace = TraceLog(tmp_path / "trace.jsonl")
    gw = DeviceGateway(transport=hub, heartbeat_interval=0.1, hub_timeout=0.5, reconnect_interval=0.05,
                       command_timeout=1.0, trace=trace.record)
    col = Collector(gw)
    assert gw.connect(timeout=2.0)
    try:
        game_ready(gw, gm.RIGHT)
        hub.ball(gm.RIGHT)
        cycle = col.wait("BALL_RECEIVED")[0].data["cycle"]
        trace.record("game", source_dial=int(gm.RIGHT), cycle=cycle, capsule_id="c-1")
        assert gm.play(gw, gm.RIGHT, "INTAKE").ok
        assert gm.play(gw, gm.RIGHT, "RETURN_FLOW").ok
        rel = gw.release_ball(gm.RIGHT)
        col.wait("RELEASE_DONE")
        assert gm.play(gw, gm.RIGHT, "FAULT").ok
        assert hub.devices[gm.RIGHT].led("FRONT")["color"] == (255, 0, 0)
        assert gm.stop(gw, gm.RIGHT).ok
    finally:
        gw.disconnect()
        hub.stop()
        trace.close()
    recs = [json.loads(line) for line in (tmp_path / "trace.jsonl").read_text().splitlines()]
    keys = {"t_mono", "session_id", "source_dial", "cycle", "command_id", "event_id", "event", "effect_id", "rgb",
            "direction", "ack", "release_state"}
    assert all(keys <= set(r) for r in recs)
    assert [r["t_mono"] for r in recs] == sorted(r["t_mono"] for r in recs)
    intake = next(r for r in recs if r["kind"] == "command" and r["effect_id"] == "CHASE")
    assert (intake["source_dial"], intake["direction"], intake["cycle"]) == (0x21, "TO_SCREEN", cycle)
    assert next(r for r in recs if r["kind"] == "ack" and r["command_id"] == intake["command_id"])["ack"] == "ACCEPTED"
    states = [r["release_state"] for r in recs if r["kind"] == "event" and r["command_id"] == rel.command_id]
    assert states == ["STARTED", "DONE"]
    assert next(r for r in recs if r["kind"] == "game")["capsule_id"] == "c-1"
    assert not [r for r in recs if r.get("command") == "GET_STATUS"]
