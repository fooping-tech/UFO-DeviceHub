"""DeviceGateway (real code) against MockHub (simulated hub + devices)."""

import asyncio
import json
import queue
import time

import pytest

from pc import DeviceGateway, DeviceId, GatewayNotConnected, ProtocolError
from pc.mock_hub import MockConfig, MockDial, MockHub

D1, D2, ILL = DeviceId.M5DIAL_1, DeviceId.M5DIAL_2, DeviceId.ILLUMINATION

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
    hub = MockHub(FAST)
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
    assert set(devs) == {ILL, D1, D2} and all(d["online"] for d in devs.values())
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
    assert set(res) == {ILL, D1, D2} and all(r.ok for r in res.values())
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
