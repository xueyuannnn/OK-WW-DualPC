import socket
import struct
import threading

import pytest

from dualpc.kmbox import CONNECT, KEYBOARD, MOVE, KMBoxError, KMBoxNet
from dualpc.settings import BoxSettings


class EchoBox:
    def __init__(self, mode="echo"):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.settimeout(0.05)
        self.mode = mode
        self.packets = []
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop.is_set():
            try:
                data, peer = self.socket.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                return
            self.packets.append(data)
            if self.mode == "silent":
                continue
            if self.mode == "delayed_ack":
                uid, random, sequence, command = struct.unpack_from("<IIII", data)
                self.socket.sendto(struct.pack("<IIII", uid, random, (sequence - 1) & 0xFFFFFFFF, command), peer)
            self.socket.sendto(data[:16], peer)

    def close(self):
        self.stop.set()
        self.socket.close()
        self.thread.join(timeout=1)


@pytest.mark.parametrize("mode", ["echo", "delayed_ack"])
def test_wire_protocol_and_keyboard_state(mode):
    server = EchoBox(mode)
    client = KMBoxNet(BoxSettings(host="127.0.0.1", port=server.socket.getsockname()[1], uuid="1234ABCD", dry_run=False))
    try:
        client.connect()
        client.key(0xE1, True)
        client.key(4, True)
        client.key(7, True)
        client.key(4, False)
        client.move(-20, 35)
        uid, _, sequence, command = struct.unpack_from("<IIII", server.packets[0])
        assert (uid, sequence, command, len(server.packets[0])) == (0x1234ABCD, 0, CONNECT, 16)
        keyboard = [p for p in server.packets if struct.unpack_from("<I", p, 12)[0] == KEYBOARD]
        assert keyboard[-1][16:] == bytes([2, 0, 7] + [0] * 9)
        move = server.packets[-1]
        assert len(move) == 72 and struct.unpack_from("<4i", move, 16) == (0, -20, 35, 0)
        client.release_all()
        assert server.packets[-1][16:] == bytes(12)
    finally:
        client.close()
        server.close()


def test_timeout_does_not_replay_movement():
    server = EchoBox()
    client = KMBoxNet(BoxSettings(host="127.0.0.1", port=server.socket.getsockname()[1], uuid="00000001", timeout=0.05, dry_run=False))
    try:
        client.connect()
        server.mode = "silent"
        before = len(server.packets)
        with pytest.raises(KMBoxError):
            client.move(123, -45)
        assert not client.connected
        assert len(server.packets) == before + 1
        assert struct.unpack_from("<I", server.packets[-1], 12)[0] == MOVE
    finally:
        client.close(release=False)
        server.close()


def test_dry_run_never_opens_socket():
    def forbidden(*args):
        raise AssertionError("socket opened during dry run")
    client = KMBoxNet(BoxSettings(), socket_factory=forbidden)
    client.connect()
    client.key(0xE0, True)
    client.key(4, True)
    assert client.modifiers == 1 and client.keys == {4}
    client.close()


def test_rollover_and_movement_bounds():
    client = KMBoxNet(BoxSettings())
    client.connect()
    for code in range(4, 14):
        client.key(code, True)
    with pytest.raises(ValueError):
        client.key(14, True)
    with pytest.raises(ValueError):
        client.move(32768, 0)
    client.close()


def test_connection_probe_acknowledges_zero_motion_and_preserves_held_state():
    server = EchoBox()
    client = KMBoxNet(BoxSettings(host="127.0.0.1", port=server.socket.getsockname()[1], uuid="00000001", dry_run=False))
    try:
        client.connect()
        client.key(4, True)
        client.button("right", True)
        client.probe()
        assert client.connected and client.keys == {4} and client.buttons == 2
        assert struct.unpack_from("<4i", server.packets[-1], 16) == (2, 0, 0, 0)
    finally:
        client.close()
        server.close()
