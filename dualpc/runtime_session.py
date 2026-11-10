"""A cooperative connection worker shared by the UI and the original start flow."""
from copy import deepcopy
import queue
import threading
import time

from ok import Logger

logger = Logger.get_logger(__name__)


class RuntimeSession:
    def __init__(self, manager, reader_factory):
        self.manager = manager
        self.reader_factory = reader_factory
        self.lock = threading.RLock()
        self.thread = None
        self.stop_event = threading.Event()
        self.commands = queue.Queue()
        self.ready = threading.Event()
        self.closed = False
        self.manually_stopped = False
        self.phase = "idle"
        self.revision = self.applied_revision = 0
        self.controller_error = ""
        self.last_check = None
        self.last_test = None
        self.test_sequence = 0
        manager.interaction.suspend()

    def pause_tasks(self, reason="双机设备配置已变更"):
        executor = self.manager.executor
        if executor is not None:
            was_paused = executor.paused
            executor.pause()
            if not was_paused:
                logger.warning(f"Automatic tasks paused: {reason}")

    def start(self, settings=None):
        settings = deepcopy(settings or self.manager.settings).validate()
        with self.lock:
            if self.closed or self.manager.exit_event.is_set():
                raise RuntimeError("程序正在退出")
            alive = self.thread is not None and self.thread.is_alive()
            if alive and self.stop_event.is_set():
                raise RuntimeError("正在释放设备，请等待线程终止后再启动")
            self.revision += 1
            revision = self.revision
            self.ready = threading.Event()
            self.phase = "starting"
            self.manually_stopped = False
            self.manager.error = ""
            self.manager.interaction.suspend()
            if not alive:
                self.stop_event = threading.Event()
                self.commands = queue.Queue()
            self.commands.put(("apply", revision, settings, self.ready))
            if not alive:
                self.thread = threading.Thread(target=self._run, name="DualPCConnection", daemon=True)
                self.thread.start()
            return revision

    def stop(self, emergency=False):
        with self.lock:
            self.manually_stopped = True
            self.manager.interaction.suspend(emergency=emergency)
            self.stop_event.set()
            alive = self.thread is not None and self.thread.is_alive()
            self.phase = "stopping" if alive else "stopped"
            self.ready.set()
            if self.last_test is not None and self.last_test["status"] == "pending":
                self.last_test = dict(self.last_test, status="cancelled", message="线程已终止，移动测试已取消")
        self.pause_tasks("连接线程已终止" if not emergency else "已触发紧急停止")

    def test_move(self):
        with self.lock:
            if self.phase != "running" or self.stop_event.is_set():
                raise RuntimeError("请先启动连接线程")
            if self.last_test is not None and self.last_test["status"] == "pending":
                raise RuntimeError("移动测试正在执行，请稍候")
            if not self.manager.interaction.client.connected:
                raise RuntimeError("盒子尚未连接，请检查参数并应用配置")
            if self.manager.executor is not None and not self.manager.executor.paused:
                raise RuntimeError("请先暂停自动任务，再测试移动")
            self.test_sequence += 1
            self.last_test = {"id": self.test_sequence, "status": "pending", "message": "正在测试移动…"}
            self.commands.put(("test", self.applied_revision, self.test_sequence, None))
            return self.test_sequence

    def _release_reader(self):
        reader = self.manager.capture_method.reader
        reader.close()
        # An unresponsive driver must not be reopened while its old read thread
        # still owns the device. The UI remains responsive in 'stopping'.
        while not reader.closed:
            if self.closed or self.manager.exit_event.is_set():
                return False
            time.sleep(0.05)
        return True

    def _disconnect(self):
        try:
            self.manager.interaction.disconnect()
        except Exception as error:
            self.controller_error = f"释放旧盒子时未获确认：{error}"
        return self._release_reader()

    def _apply(self, revision, settings):
        self.pause_tasks()
        if not self._disconnect() or self.stop_event.is_set() or self.closed or self.manager.exit_event.is_set():
            return
        m = self.manager
        m.capture_method.reader = self.reader_factory(settings.capture)
        m.capture_method._size = (settings.capture.output_width, settings.capture.output_height)
        m.interaction.reconfigure(settings.kmbox)
        m.settings = settings
        m.app_config["dualpc_settings"] = settings
        m.hwnd_window.update(settings)
        if m.executor is not None:
            m.executor._frame = None
        errors = []
        self.controller_error, self.last_check = "", None
        try:
            m.capture_method.reader.start()
        except Exception as error:
            errors.append(f"采集卡：{error}")
        if not self.stop_event.is_set() and not self.closed and not m.exit_event.is_set() and revision == self.revision:
            try:
                m.interaction.client.connect()
                self.last_check = time.monotonic()
            except Exception as error:
                self.controller_error = str(error)
                errors.append(f"控制器：{error}")
        with self.lock:
            if self.stop_event.is_set() or self.closed or m.exit_event.is_set() or revision != self.revision:
                return
            self.applied_revision = revision
            self.phase = "running"
            m.error = "；".join(errors)
            m.interaction.resume_connection()
        m._notify_devices()

    def _test(self, revision, identifier):
        with self.lock:
            if self.stop_event.is_set():
                return
            if revision != self.revision or revision != self.applied_revision:
                self.last_test = {"id": identifier, "status": "cancelled", "message": "配置已变化，请应用配置后重新测试"}
                return
        try:
            result = self.manager.interaction.test_move(40, 40)
            counts = result["counts"]
            message = ("演练已记录，未向盒子输出" if result["dry_run"] else "移动指令已发送，请观察游戏机鼠标")
            message += f" · 目标右 40 / 下 40 像素，发送横向 {counts[0]} / 纵向 {counts[1]} counts"
            result.update(id=identifier, status="passed", message=message)
        except Exception as error:
            result = {"id": identifier, "status": "failed", "message": f"移动测试失败或未获确认：{error}"}
        with self.lock:
            if not self.stop_event.is_set():
                self.last_test = result

    def _heartbeat(self):
        m = self.manager
        if self.phase != "running":
            return
        if m.interaction.client.connected:
            try:
                m.interaction.probe()
                self.last_check = time.monotonic()
                self.controller_error = ""
            except Exception as error:
                m.interaction.client.connected = False
                self.controller_error = str(error)
        if not m.capture_method.connected():
            self.pause_tasks("采集画面断开或超过有效帧龄")
        elif not m.interaction.client.connected:
            self.pause_tasks("盒子通信断开：" + self.controller_error)

    def _run(self):
        next_check = time.monotonic() + 1
        try:
            while not self.stop_event.is_set() and not self.manager.exit_event.is_set():
                try:
                    kind, revision, payload, ready = self.commands.get(timeout=0.15)
                except queue.Empty:
                    kind = None
                if self.stop_event.is_set():
                    break
                try:
                    if kind == "apply":
                        if revision == self.revision:
                            self._apply(revision, payload)
                        ready.set()
                        next_check = time.monotonic() + 1
                    elif kind == "test":
                        self._test(revision, payload)
                except Exception as error:
                    with self.lock:
                        self.manager.error = str(error)
                        self.phase = "error"
                        if ready is not None:
                            ready.set()
                if time.monotonic() >= next_check and not self.stop_event.is_set():
                    self._heartbeat()
                    next_check = time.monotonic() + 1
        finally:
            self.manager.interaction.suspend()
            try:
                self._disconnect()
            except Exception as error:
                self.manager.error = f"设备关闭失败：{error}"
            finally:
                with self.lock:
                    self.phase = "stopped"
                    self.ready.set()
            self.manager._notify_devices()
            if self.closed:
                self.manager.interaction.on_destroy()

    def snapshot(self):
        with self.lock:
            return {"phase": self.phase, "revision": self.revision, "applied_revision": self.applied_revision,
                    "controller_error": self.controller_error, "last_check": self.last_check,
                    "last_test": deepcopy(self.last_test), "error": self.manager.error,
                    "worker_alive": self.thread is not None and self.thread.is_alive()}

    def close(self):
        with self.lock:
            self.closed = True
        self.stop()
        thread = self.thread
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=3)
        if thread is None or not thread.is_alive():
            self.manager.interaction.on_destroy()
