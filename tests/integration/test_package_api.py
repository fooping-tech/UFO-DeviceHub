"""The package other projects install (pyproject.toml): its import name and the points a
caller with its own serial devices needs (port exclusion, exclusive open, link diagnostics)."""


import pytest

import pc
import ufo_devicehub
from ufo_devicehub import DeviceGateway, MockConfig, MockHub, SerialLineTransport, TransportError
from ufo_devicehub import device_gateway as gwmod


class FakeSerial:
    opened: list = []
    refuse: set = set()

    def __init__(self, port, baudrate, **kw):
        if port in FakeSerial.refuse:
            raise OSError("Resource busy")
        FakeSerial.opened.append((port, kw))

    def reset_input_buffer(self):
        pass

    def close(self):
        pass


@pytest.fixture
def fake_serial(monkeypatch):
    serial = pytest.importorskip("serial")
    FakeSerial.opened = []
    FakeSerial.refuse = set()
    monkeypatch.setattr(serial, "Serial", FakeSerial)
    return FakeSerial


def test_the_public_names_are_the_pc_package_s_own():
    for name in ufo_devicehub.__all__:
        assert hasattr(ufo_devicehub, name), name
    assert ufo_devicehub.DeviceGateway is pc.DeviceGateway
    assert ufo_devicehub.protocol.PROTOCOL_VERSION == ufo_devicehub.PROTOCOL_VERSION
    for name in pc.__all__:  # whatever `from pc import ...` gives is public here too
        assert getattr(ufo_devicehub, name) is getattr(pc, name), name


def test_auto_detect_never_opens_an_excluded_port(monkeypatch, fake_serial):
    monkeypatch.setattr(gwmod, "guess_ports", lambda: ["/dev/cu.usbserial-OTHER", "/dev/cu.usbmodem-HUB"])
    t = SerialLineTransport("auto", exclude=["/dev/cu.usbserial-OTHER", ""])
    t.open()
    assert [p for p, _ in fake_serial.opened] == ["/dev/cu.usbmodem-HUB"] and t.open_error_kind == ""
    # the hub's port was not a hub after all: the excluded one is still never tried
    t.close()
    t.reject_current()
    assert t.rejected_port == "/dev/cu.usbmodem-HUB" and t.rejected_at > 0
    monkeypatch.setattr(gwmod, "guess_ports", lambda: ["/dev/cu.usbserial-OTHER"])
    with pytest.raises(TransportError):
        t.open()
    assert t.open_error_kind == "no_port"
    assert [p for p, _ in fake_serial.opened] == ["/dev/cu.usbmodem-HUB"]


def test_a_named_port_that_is_excluded_is_refused(fake_serial):
    t = SerialLineTransport("/dev/cu.usbserial-OTHER")
    t.exclude.add("/dev/cu.usbserial-OTHER")  # learnt after construction
    with pytest.raises(TransportError, match="excluded"):
        t.open()
    assert t.open_error_kind == "excluded" and fake_serial.opened == []


def test_exclusive_is_asked_for_only_when_wanted(fake_serial):
    SerialLineTransport("/dev/cu.A").open()
    SerialLineTransport("/dev/cu.B", exclusive=True).open()
    assert "exclusive" not in fake_serial.opened[0][1]
    assert fake_serial.opened[1][1]["exclusive"] is True


def test_a_port_that_does_not_open_does_not_hide_the_hub(monkeypatch, fake_serial):
    fake_serial.refuse = {"/dev/cu.usbmodem-LOCKED"}
    monkeypatch.setattr(gwmod, "guess_ports", lambda: ["/dev/cu.usbmodem-LOCKED", "/dev/cu.usbmodem-HUB"])
    t = SerialLineTransport("auto")
    with pytest.raises(TransportError, match="LOCKED"):
        t.open()
    assert t.open_error_kind == "open_failed" and "Resource busy" in t.open_error
    t.open()  # the next attempt goes on to the other candidate
    assert [p for p, _ in fake_serial.opened] == ["/dev/cu.usbmodem-HUB"] and t.open_error == ""


def test_link_diagnostics_says_why_there_is_no_session():
    hub = MockHub(MockConfig(system_id=2))
    gw = DeviceGateway(transport=hub, heartbeat_interval=0.05, hub_timeout=0.4, reconnect_interval=0.05,
                       system_id=1)
    try:
        d = gw.link_diagnostics()
        assert d["connected"] is False and d["wrong_system"] == "" and d["wrong_system_age_s"] is None
        assert not gw.connect(timeout=0.8)
        d = gw.link_diagnostics()
        assert d["connected"] is False and d["system_id"] == 1
        assert d["wrong_system"] and 0 <= d["wrong_system_age_s"] < 5
    finally:
        gw.disconnect()


def test_link_diagnostics_when_connected():
    gw = DeviceGateway(transport=MockHub(MockConfig(system_id=1)), heartbeat_interval=0.05, system_id=1)
    try:
        assert gw.connect(timeout=2.0)
        d = gw.link_diagnostics()
        assert d["connected"] is True and d["open_error"] == "" and d["rejected_age_s"] is None
    finally:
        gw.disconnect()
