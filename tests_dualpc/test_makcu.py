import re

import pytest

from dualpc.controllers import create_controller
from dualpc.kmbox import KMBoxError
from dualpc.makcu import MakcuSerial, probe_serial
from dualpc.settings import BoxSettings, Settings


class SerialFirmware:
    """Silent V4 mutations, independently prompted queries, fragmented RX."""
    def __init__(self, route="R:MK;M:8uf;K:8uf;C:0uf", identity=b"km.MAKCU", echo=False, leading_newline=True):
        self.route, self.identity, self.echo = route, identity, echo
        self.rx = b""
        self.writes = []
        self.closed = False
        self.reject = self.drop = self.short = False
        self.held = set()
        self.leading_newline = leading_newline

    @property
    def in_waiting(self):
        return min(3, len(self.rx))

    def reset_input_buffer(self):
        self.rx = b""

    def write(self, packet):
        self.writes.append(packet)
        if self.short:
            return len(packet) - 1
        for command in packet.decode("ascii").split():
            if self.echo:
                self.rx += command.encode() + b"\r\n>>>"
            if command == "km.version()":
                if not self.drop:
                    self.rx += (b"\r\n" if self.leading_newline else b"") + self.identity + b"\r\n>>>"
            elif command == "km.device()":
                self.rx += self.route.encode() + b"\r\n>>>"
            elif self.reject:
                self.rx += b"\r\nERR\r\n>>>"
            elif match := re.fullmatch(r"km\.(down|up)\((\d+)\)", command):
                code = int(match[2])
                self.held.add(code) if match[1] == "down" else self.held.discard(code)
        return len(packet)

    def read(self, size):
        result, self.rx = self.rx[:size], self.rx[size:]
        return result

    def close(self):
        self.closed = True


class LegacySerialFirmware(SerialFirmware):
    """3.2/3.7 captured query format; unknown commands remain silent."""
    omit_ack = False

    def write(self, packet):
        self.writes.append(packet)
        for command in packet.decode('ascii').splitlines():
            if command == 'km.version()':
                self.rx += b'km.MAKCU\r\n>>> '
            elif command == 'km.left()':
                self.rx += b'km.left()\n0\r\n>>> '
            elif not self.omit_ack and re.fullmatch(
                    r'km\.(?:(?:left|right|middle|ms1|ms2)\([012]\)|move\(-?\d+,-?\d+\)|wheel\(-?\d+\))', command):
                self.rx += command.encode('ascii') + b'\r\n>>> '
        return len(packet)


def connected(firmware=None):
    firmware = firmware or SerialFirmware()
    options = {}
    def factory(*args, **kwargs):
        options.update(kwargs)
        return firmware
    client = MakcuSerial(BoxSettings(protocol="makcu_serial", dry_run=False, serial_port="COM_TEST",
                                     baudrate=4000000, timeout=0.03), factory)
    client.connect()
    return client, firmware, options


@pytest.mark.parametrize("echo", [False, True])
@pytest.mark.parametrize("leading_newline", [False, True])
def test_identity_and_route_with_fragmented_query_prompts(echo, leading_newline):
    client, serial, options = connected(SerialFirmware(echo=echo, leading_newline=leading_newline))
    assert client.connected and client.supports_keyboard and not client.supports_absolute
    assert serial.rx == b""
    assert not options["rtscts"] and not options["xonxoff"] and options["bytesize"] == 8
    client.close()
    assert serial.closed


def test_mouse_keyboard_modifiers_wheel_and_release():
    client, serial, _ = connected()
    client.key(4, True)
    client.key(0xE1, True)
    client.button("right", True)
    client.move(-40, 40)
    client.wheel(-2)
    assert serial.held == {4, 0xE1}
    assert b"km.move(-40,40)\r\nkm.version()\r\n" in serial.writes
    assert serial.writes.count(b"km.wheel(-1)\r\nkm.version()\r\n") == 2
    client.release_all()
    assert not serial.held and not client.keys and not client.modifiers and not client.buttons
    assert not any(b"km.init" in packet or b"km.baud" in packet for packet in serial.writes)
    client.close()


def test_mouse_only_route_blocks_keyboard_but_allows_connection_test():
    client, serial, _ = connected(SerialFirmware(route="R:M;M:8uf;K:0uf;C:0uf"))
    assert client.connected and not client.supports_keyboard
    client.move(40, 40)
    with pytest.raises(KMBoxError, match="仅鼠标"):
        client.key(4, True)
    client.close()


def test_legacy_32_37_connects_mouse_uses_ms_buttons_and_never_sends_keys():
    client, serial, _ = connected(LegacySerialFirmware())
    assert client.connected and client.legacy and not client.supports_keyboard
    assert client.route == 'legacy3-mouse'
    client.move(40, 40)
    client.button('x1', True)
    client.button('x2', False)
    writes_before_key = len(serial.writes)
    with pytest.raises(KMBoxError, match='不支持键盘'):
        client.key(4, True)
    assert len(serial.writes) == writes_before_key
    client.close()
    combined = b''.join(serial.writes)
    assert b'km.ms1(1)' in combined and b'km.ms2(0)' in combined
    assert not any(token in combined for token in (b'km.side', b'km.down', b'km.up(', b'km.baud', b'\xde\xad'))


def test_legacy_identity_does_not_hide_a_lost_move_ack_or_replay_input():
    client, serial, _ = connected(LegacySerialFirmware())
    serial.omit_ack = True
    before = len(serial.writes)
    with pytest.raises(KMBoxError, match='未确认指令'):
        client.move(40, 40)
    assert len(serial.writes) == before + 1 and not client.connected
    serial.omit_ack = False
    client.close()


def test_legacy_baud_probe_is_read_only():
    serial = LegacySerialFirmware()
    result = probe_serial('COM_TEST', lambda *args, **kwargs: serial)
    assert result['legacy'] and result['mouse'] and not result['keyboard']
    assert result['route'] == 'legacy3-mouse' and serial.closed
    assert all(set(packet.decode().splitlines()) <= {'km.version()', 'km.device()', 'km.left()'}
               for packet in serial.writes)


@pytest.mark.parametrize("route", ["R:K;M:0uf;K:8uf;C:0uf", "keyboard", "R:MK;M:0uf;K:8uf;C:0uf"])
def test_missing_mouse_or_unconfirmed_v4_route_fails_connection(route):
    serial = SerialFirmware(route=route)
    with pytest.raises(KMBoxError):
        connected(serial)
    assert serial.closed


def test_query_echo_is_not_a_successful_handshake():
    serial = SerialFirmware(identity=b"km.version()", echo=True)
    with pytest.raises(KMBoxError, match="身份"):
        connected(serial)
    assert serial.closed


@pytest.mark.parametrize("failure", ["drop", "reject", "short"])
def test_failed_movement_marks_disconnected_and_is_never_replayed(failure):
    client, serial, _ = connected()
    setattr(serial, failure, True)
    count = len(serial.writes)
    with pytest.raises(KMBoxError):
        client.move(40, 40)
    assert not client.connected and len(serial.writes) == count + 1
    setattr(serial, failure, False)
    client.close()
    assert sum(b"km.move(40,40)" in packet for packet in serial.writes) == 1


def test_ambiguous_key_press_is_still_released_on_close():
    client, serial, _ = connected()
    serial.drop = True
    with pytest.raises(KMBoxError):
        client.key(26, True)
    assert 26 in client.keys and 26 in serial.held
    serial.drop = False
    client.close()
    assert not serial.held


def test_reconnect_releases_and_closes_failed_transport_before_opening_another():
    client, serial, _ = connected()
    serial.drop = True
    with pytest.raises(KMBoxError):
        client.key(26, True)
    serial.drop = False
    replacement = SerialFirmware()
    def reopen(*args, **kwargs):
        assert serial.closed and not serial.held
        return replacement
    client._port_factory = reopen
    client.connect()
    assert client.connected and not client.keys and not replacement.held
    client.close()


def test_keyboard_rollover_preserves_existing_keys():
    client, serial, _ = connected()
    for key in range(4, 10):
        client.key(key, True)
    with pytest.raises(ValueError, match="six"):
        client.key(10, True)
    client.key(4, True)  # Repeated down does not consume another slot.
    assert client.keys == set(range(4, 10))
    client.close()


def test_factory_dry_run_and_required_serial_configuration():
    settings = Settings(kmbox=BoxSettings(protocol="makcu_serial"))
    settings.validate()
    client = create_controller(settings.kmbox)
    client.connect()
    client.move(40, 40)
    client.close()
    assert any(b"km.move(40,40)" in packet for packet in client.history)
    settings.kmbox.dry_run = False
    with pytest.raises(ValueError, match="COM"):
        settings.validate()


def test_baud_detection_falls_back_without_emitting_input(monkeypatch):
    ports = []
    original = MakcuSerial._transaction
    def factory(port, baudrate, **unused):
        serial = SerialFirmware()
        ports.append((baudrate, serial))
        return serial
    def transaction(client, commands=()):
        if client.settings.baudrate == 4000000:
            raise KMBoxError("Wrong baud")
        return original(client, commands)
    monkeypatch.setattr(MakcuSerial, "_transaction", transaction)
    result = probe_serial("COM_SELECTED", factory)
    assert result["baudrate"] == 115200 and result["mouse"] and result["keyboard"]
    assert [baud for baud, port in ports] == [4000000, 115200]
    assert all(port.closed for baud, port in ports)
    assert all(packet in {b"km.version()\r\n", b"km.device()\r\nkm.version()\r\n"}
               for baud, port in ports for packet in port.writes)


def test_probe_failure_closes_both_ports_and_reports_errors():
    ports = []
    def factory(*args, **kwargs):
        port = SerialFirmware(route="old firmware")
        ports.append(port)
        return port
    result = probe_serial("COM_SELECTED", factory)
    assert "error" in result and len(ports) == 2 and all(port.closed for port in ports)
    with pytest.raises(ValueError):
        probe_serial("", factory)
