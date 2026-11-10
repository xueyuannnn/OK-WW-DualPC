from copy import deepcopy
from pathlib import Path
import struct
import threading
import time
from types import SimpleNamespace

import pytest

from dualpc.bootstrap import make_config
from dualpc.capture import CaptureReader
from dualpc.kmbox import MOVE
from dualpc.settings import Settings
from tools_dualpc.benchmark_capture import SyntheticCamera


def wait_for(condition, timeout=3):
    deadline = time.monotonic() + timeout
    while not condition():
        if time.monotonic() > deadline:
            raise AssertionError("Connection worker did not reach the expected state")
        time.sleep(0.01)


class TestReader(CaptureReader):
    __test__ = False

    def __init__(self, settings):
        camera = SyntheticCamera(settings.width, settings.height, settings.fps)
        super().__init__(settings, lambda *unused: camera)

    def start(self, timeout=5):
        if self.settings.device_name == "missing-card":
            raise RuntimeError("Capture is unavailable")
        return super().start(timeout)


@pytest.fixture
def manager(monkeypatch, tmp_path):
    from dualpc import device
    from ok.util.config import Config
    from ok.util.handler import ExitEvent
    monkeypatch.setattr(device, "CaptureReader", TestReader)
    monkeypatch.setattr(Config, "config_folder", str(tmp_path))
    event = ExitEvent()
    result = device.DualPCDeviceManager(make_config(Settings(), Path(__file__).absolute().parents[1]), event)
    executor = SimpleNamespace(paused=True, current_task=None, _frame=None)
    executor.pause = lambda: setattr(executor, "paused", True)
    result.executor = executor
    yield result
    result.close()
    event.set()
    result.handler.thread.join(timeout=1)


def start(manager, settings=None):
    revision = manager.start_session(settings)
    wait_for(lambda: manager.session_snapshot()["applied_revision"] == revision)
    return revision


def test_start_stop_and_hot_apply_preserve_facades_and_pause_tasks(manager):
    capture, interaction = manager.capture_method, manager.interaction
    start(manager)
    assert manager.device_connected()
    initial_reader, initial_client = capture.reader, interaction.client
    manager.executor.paused = False
    interaction.send_key_down("w")
    interaction.mouse_down()
    manager.stop_session()
    assert manager.executor.paused
    wait_for(lambda: manager.session_snapshot()["phase"] == "stopped")
    assert initial_reader.closed and not initial_client.connected
    assert not initial_client.keys and not initial_client.buttons
    assert not interaction.should_capture()
    updated = deepcopy(manager.settings)
    updated.capture.output_width, updated.capture.output_height = 1280, 720
    updated.kmbox.timeout = 0.6
    start(manager, updated)
    assert manager.capture_method is capture and manager.interaction is interaction
    assert capture.reader is not initial_reader and interaction.client is not initial_client
    assert manager.settings == updated
    assert manager.hwnd_window.width == 1280 and manager.capture_method._size == (1280, 720)
    assert manager.executor.paused and interaction.should_capture()


def test_stopped_session_does_not_reconnect_on_background_refresh(manager):
    start(manager)
    manager.stop_session()
    wait_for(lambda: manager.session_snapshot()["phase"] == "stopped")
    manager.do_refresh()
    assert manager.session_snapshot()["phase"] == "stopped"
    assert not manager.interaction.client.connected
    with pytest.raises(RuntimeError, match="thread is stopped"):
        manager.interaction.send_key_down("e")


def test_initial_device_refresh_does_not_occupy_capture_or_connect_a_box(manager):
    initial_reader = manager.capture_method.reader
    manager.do_refresh()
    assert manager.session_snapshot()["phase"] == "idle"
    assert manager.session.thread is None and initial_reader._thread is None
    assert not manager.interaction.client.connected


def test_original_start_flow_can_explicitly_restart_a_stopped_session(manager):
    manager.stop_session()
    manager.do_refresh(current=True)
    assert manager.device_connected()


@pytest.mark.parametrize("protocol", ["kmbox_net"])
def test_all_boxes_support_one_calibrated_relative_move_in_dry_run(manager, protocol):
    settings = Settings()
    settings.kmbox.protocol = protocol
    settings.kmbox.counts_per_pixel_x, settings.kmbox.counts_per_pixel_y = 1.5, 0.5
    start(manager, settings)
    client = manager.interaction.client
    client.history.clear()
    identifier = manager.test_mouse_move()
    wait_for(lambda: manager.session_snapshot()["last_test"]["status"] != "pending")
    result = manager.session_snapshot()["last_test"]
    assert result["id"] == identifier and result["status"] == "passed"
    assert result["pixels"] == [40, 40] and result["counts"] == [60, 20] and result["dry_run"]
    assert "未向盒子输出" in result["message"]
    assert not client.buttons and not client.keys
    if protocol == "kmbox_net":
        moves = [struct.unpack_from("<4i", p, 16)[1:3] for p in client.history if struct.unpack_from("<I", p, 12)[0] == MOVE]
        assert [m for m in moves if m != (0, 0)] == [(60, 20)]


def test_box_test_can_run_without_video_but_gameplay_cannot(manager):
    settings = Settings()
    settings.capture.device_name = "missing-card"
    start(manager, settings)
    assert not manager.capture_method.connected() and manager.interaction.client.connected
    assert "采集卡" in manager.session_snapshot()["error"]
    manager.test_mouse_move()
    wait_for(lambda: manager.session_snapshot()["last_test"]["status"] == "passed")
    manager.executor.paused = False
    with pytest.raises(RuntimeError, match="stale"):
        manager.interaction.send_key_down("w")


def test_box_connection_failure_keeps_video_available_and_blocks_tests(manager, monkeypatch):
    from dualpc import interaction
    original = interaction.create_controller

    def failing_controller(settings):
        client = original(settings)
        client.connect = lambda: (_ for _ in ()).throw(RuntimeError("Box unavailable"))
        return client

    monkeypatch.setattr(interaction, "create_controller", failing_controller)
    start(manager)
    assert manager.capture_method.connected()
    assert not manager.interaction.client.connected
    assert "Box unavailable" in manager.session_snapshot()["controller_error"]
    with pytest.raises(RuntimeError, match="尚未连接"):
        manager.test_mouse_move()


def test_pending_test_is_not_duplicated_or_run_after_stop(manager, monkeypatch):
    start(manager)
    entered, proceed = threading.Event(), threading.Event()
    original = manager.interaction.test_move

    def delayed(*args):
        entered.set()
        assert proceed.wait(2)
        return original(*args)

    monkeypatch.setattr(manager.interaction, "test_move", delayed)
    client = manager.interaction.client
    client.history.clear()
    manager.test_mouse_move()
    assert entered.wait(1)
    with pytest.raises(RuntimeError, match="正在执行"):
        manager.test_mouse_move()
    manager.stop_session()
    proceed.set()
    wait_for(lambda: manager.session_snapshot()["phase"] == "stopped")
    assert manager.session_snapshot()["last_test"]["status"] == "cancelled"
    moves = [struct.unpack_from("<4i", p, 16)[1:3] for p in client.history if struct.unpack_from("<I", p, 12)[0] == MOVE]
    assert all(m == (0, 0) for m in moves)


def test_movement_test_requires_paused_automation(manager):
    start(manager)
    manager.executor.paused = False
    with pytest.raises(RuntimeError, match="暂停自动任务"):
        manager.test_mouse_move()
    with pytest.raises(RuntimeError, match="暂停自动任务"):
        manager.interaction.test_move()


def test_latest_configuration_wins_and_no_overlapping_readers_are_left(manager):
    entered, proceed = threading.Event(), threading.Event()
    readers = []

    class DelayedReader(TestReader):
        def start(self, timeout=5):
            if self.settings.fps == 75:
                entered.set()
                assert proceed.wait(2)
            return super().start(timeout)

    def factory(settings):
        reader = DelayedReader(settings)
        readers.append(reader)
        return reader

    manager.session.reader_factory = factory
    one, two = Settings(), Settings()
    one.capture.fps, two.capture.fps = 75, 120
    manager.start_session(one)
    assert entered.wait(1)
    revision = manager.start_session(two)
    proceed.set()
    wait_for(lambda: manager.session_snapshot()["applied_revision"] == revision)
    assert manager.settings.capture.fps == 120
    assert readers[0].closed and not readers[-1].closed


def test_emergency_stop_can_only_be_rearmed_by_explicit_session_start(manager):
    start(manager)
    manager.stop_session(emergency=True)
    wait_for(lambda: manager.session_snapshot()["phase"] == "stopped")
    assert manager.interaction._emergency.is_set()
    start(manager)
    assert not manager.interaction._emergency.is_set()
    assert manager.interaction.should_capture()


def test_kmbox_heartbeat_detects_lost_ack_and_pauses_automation(manager):
    from tests_dualpc.test_transport import EchoBox
    server = EchoBox()
    settings = Settings()
    settings.kmbox.dry_run = False
    settings.kmbox.host = "127.0.0.1"
    settings.kmbox.port = server.socket.getsockname()[1]
    settings.kmbox.uuid = "12345678"
    settings.kmbox.timeout = 0.05
    try:
        start(manager, settings)
        assert manager.interaction.client.connected
        assert manager.session_snapshot()["last_check"] is not None
        manager.executor.paused = False
        server.mode = "silent"
        wait_for(lambda: not manager.interaction.client.connected, timeout=3)
        wait_for(lambda: manager.executor.paused)
        assert manager.session_snapshot()["controller_error"]
        assert manager.capture_method.connected()
    finally:
        manager.stop_session()
        wait_for(lambda: manager.session_snapshot()["phase"] == "stopped")
        server.close()
