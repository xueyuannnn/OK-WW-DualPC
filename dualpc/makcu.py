"""MAKCU V4 text API, using query barriers for silent mutation commands.

Reference: https://makcu.com/en/api/ (V4 command table). No baud changes or
binary opcodes from the incompatible legacy AIO protocol are sent.
"""
import re
import time

from .kmbox import KMBoxError, KMBoxNet


class MakcuSerial(KMBoxNet):
    supports_absolute = False
    supports_keyboard = False
    max_keys = 6
    BUTTONS = {"left": (1, "left"), "right": (2, "right"), "middle": (4, "middle"),
               "x1": (8, "side1"), "x2": (16, "side2")}

    def __init__(self, settings, port_factory=None):
        super().__init__(settings)
        self._port_factory = port_factory
        self._port = None
        self.route = ""
        self.capability_status = "MAKCU V4：尚未检测键盘路由"

    def _transaction(self, commands=()):
        # A version result after the mutation proves the command processor
        # reached the barrier. Never retransmit movement after an ambiguous ACK.
        packet = "\r\n".join([*commands, "km.version()", ""]).encode("ascii")
        if self.settings.dry_run:
            self.history.append(packet)
            del self.history[:-256]
            return "R:MK;M:8uf;K:8uf;C:0uf\r\nkm.MAKCU\r\n>>>"
        if self._port is None:
            raise KMBoxError("MAKCU 串口未打开")
        try:
            if self._port.write(packet) != len(packet):
                raise IOError("串口报文未完整写入")
            data = bytearray()
            deadline = time.monotonic() + self.settings.timeout
            while time.monotonic() < deadline and len(data) < 32768:
                chunk = self._port.read(max(1, min(4096, self._port.in_waiting)))
                if chunk:
                    data.extend(chunk)
                    text = data.decode("ascii", errors="replace")
                    # V27/V29 may echo the query; the echo is not an identity.
                    identity = re.search(r"(?:^|[\r\n]|>>>)\s*km\.MAKCU[\r\n]+", text)
                    if identity and ">>>" in text[identity.end():]:
                        if re.search(r"(?:^|[\r\n>\s])ERR\b", text):
                            raise IOError("固件拒绝命令（ERR），请核对 V4 固件和已连接的键鼠设备")
                        return text
            raise TimeoutError("未收到 MAKCU 身份及完整应答，请核对 COM 口、波特率和 V4 固件")
        except (OSError, ValueError) as error:
            self.connected = False
            raise KMBoxError(f"MAKCU 通信失败：{error}") from error

    def connect(self):
        with self._lock:
            if self.connected:
                return
            if self._port is not None:
                self.close()  # Release ambiguous presses before reopening a failed transport.
            self.supports_keyboard = False
            try:
                if not self.settings.dry_run:
                    if self._port_factory is None:
                        from serial import Serial
                        self._port_factory = Serial
                    self._port = self._port_factory(
                        self.settings.serial_port, self.settings.baudrate,
                        timeout=min(self.settings.timeout, 0.05), write_timeout=self.settings.timeout,
                        bytesize=8, parity="N", stopbits=1, xonxoff=False, rtscts=False, dsrdtr=False)
                    self._port.reset_input_buffer()
                self._transaction()
                response = self._transaction(["km.device()"])
                route = re.search(r"\bR:([MKC-]+);M:(\d+)uf;K:(\d+)uf;C:(\d+)uf", response)
                if route is None:
                    raise KMBoxError("未取得 MAKCU V4 路由信息，请核对所用固件协议")
                self.route = route.group(0)
                if "M" not in route[1] or int(route[2]) <= 0:
                    raise KMBoxError("MAKCU 未检测到可用鼠标输出，请检查盒子与游戏机及鼠标的连接")
                self.supports_keyboard = "K" in route[1] and int(route[3]) > 0
                self.capability_status = ("MAKCU V4：鼠标与键盘路由已确认" if self.supports_keyboard else
                                          "MAKCU V4：仅鼠标路由；接入键盘后重新应用配置才能运行自动任务")
                self.connected = True
                self.release_all()
            except Exception:
                self.close(release=False)
                raise

    def probe(self):
        with self._lock:
            if not self.connected:
                raise KMBoxError("MAKCU 未连接")
            self._transaction()

    def move(self, x, y):
        with self._lock:
            if int(x) != x or int(y) != y or not -32767 <= x <= 32767 or not -32767 <= y <= 32767:
                raise ValueError("MAKCU relative movement must fit signed 16-bit HID coordinates")
            self._transaction([f"km.move({int(x)},{int(y)})"])

    def button(self, name, down):
        bit, command = self.BUTTONS[name]
        with self._lock:
            # Remember a possibly applied press even when its barrier is lost,
            # so close() still attempts the matching release.
            if down:
                self.buttons |= bit
            self._transaction([f"km.{command}({1 if down else 0})"])
            if not down:
                self.buttons &= ~bit

    def key(self, code, down):
        if not self.supports_keyboard:
            raise KMBoxError(self.capability_status)
        if not isinstance(code, int) or not 1 <= code <= 255:
            raise ValueError("Invalid USB HID key code")
        with self._lock:
            modifier = 0xE0 <= code <= 0xE7
            if down:
                if modifier:
                    self.modifiers |= 1 << (code - 0xE0)
                else:
                    if len(self.keys) >= self.max_keys and code not in self.keys:
                        raise ValueError("MAKCU supports at most six held non-modifier keys")
                    self.keys.add(code)
            self._transaction([f"km.{'down' if down else 'up'}({code})"])
            if not down:
                if modifier:
                    self.modifiers &= ~(1 << (code - 0xE0))
                else:
                    self.keys.discard(code)

    def wheel(self, count):
        with self._lock:
            # One step per command also matches older text wheel semantics.
            for _ in range(abs(int(count))):
                self._transaction([f"km.wheel({1 if count > 0 else -1})"])

    def release_all(self):
        with self._lock:
            commands = [f"km.{command}(0)" for _, command in self.BUTTONS.values()]
            # Release only our tracked keyboard presses: km.init() would also
            # reset the user's keyboard policies on V4.
            codes = sorted(self.keys) + [0xE0 + i for i in range(8) if self.modifiers & (1 << i)]
            commands.extend(f"km.up({code})" for code in codes)
            self._transaction(commands)
            self.buttons = self.modifiers = 0
            self.keys.clear()

    def close(self, release=True):
        with self._lock:
            try:
                if release and (self.connected or self._port is not None):
                    self.release_all()
            finally:
                if self._port is not None:
                    self._port.close()
                self._port = None
                self.connected = False
                self.supports_keyboard = False


def probe_serial(port, port_factory=None):
    """Query one explicitly selected COM port; never emit a HID mutation."""
    from .settings import BoxSettings
    if not port or not port.strip():
        raise ValueError("请选择 MAKCU 对应的 COM 口")
    attempts = []
    for baud in (4000000, 115200):
        client = MakcuSerial(BoxSettings(protocol="makcu_serial", serial_port=port, baudrate=baud,
                                         dry_run=False, timeout=0.4), port_factory)
        try:
            factory = port_factory
            if factory is None:
                from serial import Serial
                factory = Serial
            client._port = factory(port, baud, timeout=0.05, write_timeout=0.4, bytesize=8, parity="N",
                                   stopbits=1, xonxoff=False, rtscts=False, dsrdtr=False)
            client._port.reset_input_buffer()
            client._transaction()
            route = client._transaction(["km.device()"])
            match = re.search(r"\bR:([MKC-]+);M:(\d+)uf;K:(\d+)uf;C:(\d+)uf", route)
            if match is None:
                raise KMBoxError("身份已识别，但未取得 V4 路由信息")
            return {"port": port, "baudrate": baud, "identity": "MAKCU", "route": match.group(0),
                    "mouse": "M" in match[1] and int(match[2]) > 0,
                    "keyboard": "K" in match[1] and int(match[3]) > 0}
        except (KMBoxError, OSError, ValueError) as error:
            attempts.append(f"{baud}：{error}")
        finally:
            client.close(release=False)
    return {"error": "未检测到可用的 MAKCU V4：" + "；".join(attempts)}
