import threading

from ok.core.events import communicate
from ok.device.DeviceManager import DeviceManager
from ok.device.capture_methods.base import BaseCaptureMethod

from .capture import CaptureReader
from .interaction import KMBoxInteraction
from .runtime_session import RuntimeSession


class CaptureCardMethod(BaseCaptureMethod):
    name = "External video capture"

    def __init__(self, settings, exit_event):
        super().__init__()
        self.exit_event = exit_event
        self.reader = CaptureReader(settings)
        self._size = (settings.output_width, settings.output_height)

    def do_get_frame(self):
        return self.reader.get_frame()

    def connected(self):
        return self.reader.connected()

    def clickable(self):
        return self.connected()

    def close(self):
        self.reader.close()


class RemoteWindow:
    """Metadata only: never represents a native window on the inference PC."""
    hwnd = top_hwnd = pid = 0
    exists = visible = pos_valid = True
    exe_full_path = ""
    frame_aspect_ratio = 16 / 9

    def __init__(self, settings):
        self.update(settings)

    def update(self, settings):
        self.hwnd_title = {"zh_CN": "鸣潮", "zh_TW": "鳴潮", "en_US": "Wuthering Waves"}[settings.game_language]
        self.title = self.hwnd_title
        self.width, self.height = settings.capture.output_width, settings.capture.output_height
        self.window_width, self.window_height = self.width, self.height

    def stop(self):
        pass


class DualPCDeviceManager(DeviceManager):
    def __init__(self, app_config, exit_event, global_config=None):
        # Base bookkeeping without constructing HwndWindow, enumerating local
        # games, starting ADB or creating a local input backend.
        base_config = dict(app_config, windows=None, adb=None, browser=None)
        super().__init__(base_config, exit_event, global_config)
        self.settings = app_config["dualpc_settings"]
        self.app_config = app_config
        self._connection_lock = threading.RLock()
        self.windows_capture_config = app_config["windows"]
        self.hwnd_window = RemoteWindow(self.settings)
        self.capture_method = CaptureCardMethod(self.settings.capture, exit_event)
        self.interaction = KMBoxInteraction(self.capture_method, self.settings.kmbox)
        self.interaction.manager = self
        self.error = ""
        self.config["capture"] = "ExternalCapture"
        self.config["interaction"] = "HardwareHID"
        self.config["preferred"] = "dualpc"
        self.session = RuntimeSession(self, lambda settings: CaptureReader(settings))
        self._describe()

    def _describe(self):
        c = self.settings.capture
        mode = "演练模式，不输出键鼠" if self.settings.kmbox.dry_run else self.settings.kmbox.protocol
        self.device_dict = {"dualpc": {"imei": "dualpc", "device": "windows", "address": "",
                            "nick": f"{c.device_name or ('视频设备 #' + str(c.device_index))} / {c.backend} + {mode}", "connected": self.device_connected(),
                            "resolution": f"{c.output_width}x{c.output_height}",
                            "width": c.output_width, "height": c.output_height,
                            "full_path": None, "real_hwnd": 0, "hwnd": "远程游戏机"}}

    def _notify_devices(self):
        self._describe()
        communicate.adb_devices.emit(True)

    def start_session(self, settings=None):
        return self.session.start(settings)

    def stop_session(self, emergency=False):
        self.session.stop(emergency=emergency)

    def test_mouse_move(self):
        return self.session.test_move()

    def session_snapshot(self):
        return self.session.snapshot()

    def do_refresh(self, current=False):
        snapshot = self.session.snapshot()
        if not current and (self.session.manually_stopped or snapshot["phase"] == "idle"):
            self._notify_devices()
            return
        if snapshot["phase"] not in {"starting", "stopping"} and (not self.device_connected() or snapshot["phase"] != "running"):
            self.start_session()
        if current:
            if not self.session.ready.wait(10):
                raise RuntimeError("连接线程尚未就绪，请检查双机设置")
            if not self.device_connected():
                raise RuntimeError(self.error or self.capture_method.reader.error or "控制器未连接")
        self._notify_devices()

    def do_start(self, notify=True):
        self.do_refresh()

    def device_connected(self):
        return self.capture_method.connected() and self.interaction.client.connected

    def update_pc_device(self):
        self._describe()
        return "dualpc"

    def update_resolution_for_hwnd(self):
        pass

    def stop_hwnd(self):
        # Process-based close/relaunch on the game PC is deliberately absent.
        pass

    def get_exe_path(self, device):
        return None

    def set_capture(self, capture):
        if capture != "ExternalCapture":
            raise ValueError("Only the external capture backend is available in dual-PC mode")

    def set_interaction(self, interaction):
        if interaction != "HardwareHID":
            raise ValueError("Only the selected external HID protocol is available in dual-PC mode")

    def close(self):
        self.session.close()
