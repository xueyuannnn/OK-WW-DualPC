from __future__ import annotations

import threading
import time

from ok.device.interaction_methods.base import BaseInteraction

from .controllers import create_controller


KEYS = {"enter": 0x28, "return": 0x28, "esc": 0x29, "escape": 0x29,
        "backspace": 0x2A, "tab": 0x2B, "space": 0x2C, "capslock": 0x39,
        "insert": 0x49, "home": 0x4A, "pageup": 0x4B, "delete": 0x4C,
        "end": 0x4D, "pagedown": 0x4E, "right": 0x4F, "left": 0x50,
        "down": 0x51, "up": 0x52, "ctrl": 0xE0, "lctrl": 0xE0,
        "control": 0xE0, "lcontrol": 0xE0, "rctrl": 0xE4, "rcontrol": 0xE4,
        "shift": 0xE1, "lshift": 0xE1, "rshift": 0xE5, "alt": 0xE2,
        "lalt": 0xE2, "ralt": 0xE6, "win": 0xE3, "cmd": 0xE3,
        "shift_l": 0xE1, "shift_r": 0xE5, "alt_l": 0xE2, "alt_r": 0xE6,
        "ctrl_l": 0xE0, "ctrl_r": 0xE4, "page_up": 0x4B, "page_down": 0x4E}
KEYS.update({chr(97 + i): 4 + i for i in range(26)})
KEYS.update({str(i): 0x1D + i for i in range(1, 10)})
KEYS["0"] = 0x27
KEYS.update({f"f{i}": 0x39 + i for i in range(1, 13)})
PUNCTUATION = {"-": 0x2D, "=": 0x2E, "[": 0x2F, "]": 0x30, "\\": 0x31,
               ";": 0x33, "'": 0x34, "`": 0x35, ",": 0x36, ".": 0x37, "/": 0x38}
KEYS.update(PUNCTUATION)
SHIFTED = dict(zip("!@#$%^&*()_+{}|:\"~<>?", "1234567890-=[]\\;'`,./"))


class KMBoxInteraction(BaseInteraction):
    is_remote = True
    # Upstream clicks can last only 10 ms, intended for window messages. Real
    # USB reports must reach the game across at least a rendered frame.
    UI_MOVE_SETTLE = 0.06
    UI_PRESS_MINIMUM = 0.04
    PRESS_MINIMUM = 0.02

    def __init__(self, capture, settings, client=None):
        super().__init__(capture)
        self.settings = settings
        self.client = client or create_controller(settings)
        self.manager = None
        self.mouse_pos = None
        self._closed = threading.Event()
        self._emergency = threading.Event()
        self._suspended = threading.Event()
        self._action_lock = threading.RLock()
        self.last_error = ""
        self._watcher = threading.Thread(target=self._watch, name="KMBoxInputRelease", daemon=True)
        self._watcher.start()

    def _watch(self):
        while not self._closed.wait(0.05):
            executor = getattr(self.manager, "executor", None)
            task = getattr(executor, "current_task", None)
            inactive = (not self.capture.connected() or self._emergency.is_set() or self._suspended.is_set()
                        or not self.client.connected
                        or (executor is not None and executor.paused)
                        or (task is not None and (task.paused or not task.enabled)))
            if inactive and (self.client.buttons or self.client.keys or self.client.modifiers):
                try:
                    with self._action_lock:
                        self.client.release_all()
                        self.mouse_pos = None
                except Exception as error:
                    self.last_error = str(error)

    def _require_active(self):
        if self._closed.is_set() or self._emergency.is_set():
            raise RuntimeError("Remote input is stopped; restart the dual-PC session to resume")
        if self._suspended.is_set():
            raise RuntimeError("Connection thread is stopped or applying settings")
        executor = getattr(self.manager, "executor", None)
        task = getattr(executor, "current_task", None)
        if executor is not None and (executor.paused or (task is not None and (task.paused or not task.enabled))):
            raise RuntimeError("Remote input is paused")
        if not self.capture.connected():
            raise RuntimeError("Capture signal is unavailable or stale; refusing remote input")
        if not self.client.connected:
            self.client.connect()

    def should_capture(self):
        return not self._emergency.is_set() and not self._closed.is_set() and not self._suspended.is_set()

    def suspend(self, emergency=False):
        # A stop request must not wait for an in-flight hardware acknowledgement.
        self._suspended.set()
        if emergency:
            self._emergency.set()

    def reconfigure(self, settings):
        """Replace a transport while retaining the interaction/capture facade."""
        with self._action_lock:
            self.client.close()
            self.settings = settings
            self.client = create_controller(settings)
            self.mouse_pos = None
            self.last_error = ""
            self._emergency.clear()

    def resume_connection(self):
        self._suspended.clear()

    def disconnect(self):
        self.suspend()
        with self._action_lock:
            self.client.close()
            self.mouse_pos = None

    def probe(self):
        with self._action_lock:
            if not self._suspended.is_set() and not self._emergency.is_set():
                self.client.probe()

    def test_move(self, dx=40, dy=40):
        """One relative displacement; no corner homing or absolute repositioning.

        This intentional connection test can run with automation paused and
        without video. Pixels are a calibrated target, not cursor feedback.
        """
        with self._action_lock:
            if self._closed.is_set() or self._emergency.is_set() or self._suspended.is_set():
                raise RuntimeError("请先启动连接线程")
            executor = getattr(self.manager, "executor", None)
            if executor is not None and not executor.paused:
                raise RuntimeError("请先暂停自动任务，再测试移动")
            if not self.client.connected:
                raise RuntimeError("盒子尚未连接，请检查连接参数并应用配置")
            counts = (round(dx * self.settings.counts_per_pixel_x), round(dy * self.settings.counts_per_pixel_y))
            try:
                self.client.release_all()
                self.client.move(*counts)
            finally:
                self.mouse_pos = None
                self.client.release_all()
            return {"dry_run": self.settings.dry_run, "pixels": [dx, dy], "counts": list(counts),
                    "protocol": self.settings.protocol}

    @staticmethod
    def key_code(key):
        try:
            return KEYS[str(key).lower()]
        except KeyError:
            raise ValueError(f"Unsupported USB HID key: {key}") from None

    def send_key_down(self, key):
        with self._action_lock:
            self._require_active()
            self.client.key(self.key_code(key), True)

    def send_key_up(self, key):
        with self._action_lock:
            if self.client.connected:
                self.client.key(self.key_code(key), False)

    def send_key(self, key, down_time=0.02):
        code = self.key_code(key)
        held = code in self.client.keys or (0xE0 <= code <= 0xE7 and self.client.modifiers & (1 << (code - 0xE0)))
        self.send_key_down(key)
        try:
            time.sleep(max(0, down_time))
        finally:
            if not held:
                self.send_key_up(key)

    def _relative_counts(self, dx, dy, precise=False):
        b = self.settings
        # Pace UI positioning reports instead of sending several large deltas
        # faster than the game's/USB mouse polling loop can consume them.
        chunk = min(b.move_chunk, 127) if precise else b.move_chunk
        interval = max(b.move_interval, .008) if precise else b.move_interval
        steps = max(1, (max(abs(dx), abs(dy)) + chunk - 1) // chunk)
        sent_x = sent_y = 0
        for i in range(1, steps + 1):
            self._require_active()
            target_x, target_y = round(dx * i / steps), round(dy * i / steps)
            self.client.move(target_x - sent_x, target_y - sent_y)
            sent_x, sent_y = target_x, target_y
            if interval and i < steps:
                time.sleep(interval)

    def move(self, x, y):
        with self._action_lock:
            self._require_active()
            b = self.settings
            target = self.capture.reader.screen_point(x, y, b.desktop_width, b.desktop_height)
            if self.client.supports_absolute:
                self.client.absolute(*target, b.desktop_width, b.desktop_height)
                self.mouse_pos = (x, y)
                return
            if not self.client.buttons:
                # KMBox Net provides relative HID movement only. Re-home before
                # each UI move; the game PC must use a single display, 6/11
                # pointer speed, no pointer acceleration, plus measured scales.
                self.client.move(-b.home_counts, -b.home_counts)
                # A protocol ACK does not mean the game has consumed the HID
                # report. Separate corner homing from the target displacement,
                # including after the game has recentered the pointer.
                self._suspended.wait(b.home_wait)
                self._require_active()
                origin = (0, 0)
            else:
                if self.mouse_pos is None:
                    raise RuntimeError("Cannot drag without a known initial cursor position")
                origin = self.capture.reader.screen_point(*self.mouse_pos, b.desktop_width, b.desktop_height)
            self._relative_counts(round((target[0] - origin[0]) * b.counts_per_pixel_x),
                                  round((target[1] - origin[1]) * b.counts_per_pixel_y),
                                  precise=not self.client.buttons)
            self.mouse_pos = (x, y)

    def mouse_down(self, x=-1, y=-1, name=None, key="left"):
        with self._action_lock:
            self._require_active()
            if x >= 0 or y >= 0:
                self.move(x, y)
                self._suspended.wait(self.UI_MOVE_SETTLE)
                self._require_active()
            self.client.button(key, True)

    def mouse_up(self, name=None, key="left"):
        with self._action_lock:
            if self.client.connected:
                self.client.button(key, False)

    def click(self, x=-1, y=-1, move_back=False, name=None, move=True, down_time=0.05, key="left"):
        positioned = move and (x >= 0 or y >= 0)
        with self._action_lock:
            old = self.mouse_pos
            if positioned:
                self.move(x, y)
                self._suspended.wait(self.UI_MOVE_SETTLE)
                self._require_active()
            self.mouse_down(key=key)
        try:
            time.sleep(max(self.UI_PRESS_MINIMUM if positioned else self.PRESS_MINIMUM, down_time))
        finally:
            self.mouse_up(key=key)
        if move_back and old is not None:
            self.move(*old)

    def scroll(self, x, y, scroll_amount):
        with self._action_lock:
            self._require_active()
            if x >= 0 and y >= 0:
                self.move(x, y)
                self._suspended.wait(self.UI_MOVE_SETTLE)
                self._require_active()
            self.client.wheel(scroll_amount)

    def swipe(self, from_x, from_y, to_x, to_y, duration, settle_time=0):
        self.move(from_x, from_y)
        self.mouse_down()
        try:
            # OK task.swipe supplies milliseconds.
            steps = max(1, round(duration / 10))
            for i in range(1, steps + 1):
                self.move(from_x + (to_x - from_x) * i / steps, from_y + (to_y - from_y) * i / steps)
                time.sleep(max(0, duration) / steps / 1000)
        finally:
            self.mouse_up()
        time.sleep(max(0, settle_time))

    def input_text(self, text):
        # Prevalidate the entire string before emitting any input. USB HID is
        # not a Unicode text API; no clipboard access on the game PC is assumed.
        strokes = []
        for char in text:
            base = SHIFTED.get(char, char.lower())
            base = {" ": "space", "\n": "enter", "\t": "tab"}.get(base, base)
            self.key_code(base)
            strokes.append((base, char.isupper() or char in SHIFTED))
        for base, shifted in strokes:
            already_shifted = bool(self.client.modifiers & 2)
            if shifted:
                self.send_key_down("shift")
            try:
                self.send_key(base)
            finally:
                if shifted and not already_shifted:
                    self.send_key_up("shift")

    def on_run(self):
        self._require_active()
        if not self.client.supports_keyboard:
            raise RuntimeError(getattr(self.client, "capability_status",
                               "所选盒子固件不支持通用键盘指令，请使用具备键盘输出能力的设备运行自动任务"))

    def emergency_stop(self):
        self._emergency.set()
        with self._action_lock:
            if self.client.connected:
                self.client.release_all()

    def on_destroy(self):
        self._closed.set()
        with self._action_lock:
            self.client.close()
        if self._watcher is not threading.current_thread():
            self._watcher.join(timeout=1)
