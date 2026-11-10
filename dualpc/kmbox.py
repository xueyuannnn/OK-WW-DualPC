"""Small KMBox Net UDP transport using the published command layouts.

No firmware/VID/PID changes, input masking, encrypted protocol or aim logic.
"""
from __future__ import annotations

import secrets
import socket
import struct
import threading
import time

CONNECT = 0xAF3C2828
MOVE = 0xAEDE7345
LEFT = 0x9823AE8D
RIGHT = 0x238D8212
MIDDLE = 0x97A3AE8D
WHEEL = 0xFFEEAD38
KEYBOARD = 0x123C2C2F


class KMBoxError(RuntimeError):
    pass


class KMBoxNet:
    supports_keyboard = True
    supports_absolute = False
    max_keys = 10
    def __init__(self, settings, socket_factory=socket.socket):
        self.settings = settings
        self._socket_factory = socket_factory
        self._socket = None
        self._lock = threading.RLock()
        self._index = 0
        self._uuid = int(settings.uuid, 16) if settings.uuid else 0
        self.connected = False
        self.buttons = 0
        self.modifiers = 0
        self.keys = set()
        self.history = []

    def connect(self):
        with self._lock:
            if self.connected:
                return
            if not self.settings.dry_run:
                self._socket = self._socket_factory(socket.AF_INET, socket.SOCK_DGRAM)
                self._socket.settimeout(self.settings.timeout)
                self._socket.connect((self.settings.host, self.settings.port))
            self._index = 0
            try:
                self._exchange(CONNECT)
                self.connected = True
                self.release_all()
            except Exception:
                self.close(release=False)
                raise

    def _exchange(self, command, payload=b""):
        index = self._index
        self._index = (index + 1) & 0xFFFFFFFF
        packet = struct.pack("<IIII", self._uuid, secrets.randbits(32), index, command) + payload
        if self.settings.dry_run:
            self.history.append(packet)
            if len(self.history) > 256:
                del self.history[:-256]
            return
        if self._socket is None:
            raise KMBoxError("KMBox Net is not connected")
        try:
            self._socket.send(packet)
            deadline = time.monotonic() + self.settings.timeout
            # Ignore delayed ACKs from earlier packets; never retransmit mouse
            # deltas blindly, since a lost ACK does not imply a lost movement.
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("No matching acknowledgement")
                self._socket.settimeout(remaining)
                reply = self._socket.recv(2048)
                if len(reply) < 16:
                    continue
                _, _, reply_index, reply_command = struct.unpack_from("<IIII", reply)
                if (reply_index, reply_command) == (index, command):
                    return
        except (OSError, TimeoutError) as error:
            self.connected = False
            raise KMBoxError(f"KMBox Net command failed: {error}") from error

    def _mouse(self, command, x=0, y=0, wheel=0):
        self._exchange(command, struct.pack("<14i", self.buttons, x, y, wheel, *([0] * 10)))

    def move(self, x, y):
        with self._lock:
            if not -32767 <= x <= 32767 or not -32767 <= y <= 32767:
                raise ValueError("KMBox relative movement must fit signed 16-bit HID coordinates")
            self._mouse(MOVE, int(x), int(y))

    def probe(self):
        """Acknowledge a zero-displacement report without altering held state."""
        with self._lock:
            if not self.connected:
                raise KMBoxError("KMBox Net is not connected")
            if not self.settings.dry_run:
                self._mouse(MOVE)

    def button(self, name, down):
        bit, command = {"left": (1, LEFT), "right": (2, RIGHT), "middle": (4, MIDDLE),
                        "x1": (8, RIGHT), "x2": (16, RIGHT)}[name]
        with self._lock:
            self.buttons = self.buttons | bit if down else self.buttons & ~bit
            self._mouse(command)

    def wheel(self, count):
        with self._lock:
            self._mouse(WHEEL, wheel=int(count))

    def key(self, code, down):
        with self._lock:
            if 0xE0 <= code <= 0xE7:
                bit = 1 << (code - 0xE0)
                self.modifiers = self.modifiers | bit if down else self.modifiers & ~bit
            elif down:
                if len(self.keys) >= self.max_keys and code not in self.keys:
                    raise ValueError(f"Controller supports at most {self.max_keys} simultaneous non-modifier keys")
                self.keys.add(code)
            else:
                self.keys.discard(code)
            keys = sorted(self.keys)
            self._exchange(KEYBOARD, bytes([self.modifiers, 0] + keys + [0] * (10 - len(keys))))

    def release_all(self):
        with self._lock:
            self.buttons = self.modifiers = 0
            self.keys.clear()
            errors = []
            for command, payload in ((MOVE, struct.pack("<14i", *([0] * 14))), (KEYBOARD, bytes(12))):
                try:
                    self._exchange(command, payload)
                except KMBoxError as error:
                    errors.append(error)
            if errors:
                raise errors[0]

    def close(self, release=True):
        with self._lock:
            try:
                if release and (self.connected or self._socket is not None):
                    self.release_all()
            finally:
                if self._socket is not None:
                    self._socket.close()
                self._socket = None
                self.connected = False
