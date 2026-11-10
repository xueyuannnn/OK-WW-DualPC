import struct
import threading
import time
from types import SimpleNamespace

import pytest

from dualpc.capture import CaptureReader
from dualpc.interaction import KMBoxInteraction
from dualpc.kmbox import KEYBOARD, MOVE
from dualpc.settings import BoxSettings, CaptureSettings


class FakeCapture:
    def __init__(self):
        self.fresh = True
        self.reader = CaptureReader(CaptureSettings())
        self.reader.raw_size = (1920, 1080)

    def connected(self):
        return self.fresh


@pytest.fixture
def interaction():
    backend = KMBoxInteraction(FakeCapture(), BoxSettings(move_interval=0))
    yield backend
    backend.on_destroy()


def movements(interaction):
    return [struct.unpack_from("<4i", p, 16) for p in interaction.client.history if struct.unpack_from("<I", p, 12)[0] == MOVE]


def test_coordinate_click_and_drag_no_rehoming_while_pressed(interaction):
    interaction.click(960, 540, down_time=0)
    moves = movements(interaction)
    assert moves[1] == (0, -32767, -32767, 0)
    assert sum(m[1] for m in moves[2:]) == 960
    assert sum(m[2] for m in moves[2:]) == 540
    interaction.move(100, 100)
    interaction.mouse_down()
    before = len(movements(interaction))
    interaction.move(200, 150)
    assert movements(interaction)[before:] == [(1, 100, 50, 0)]
    interaction.mouse_up()


def test_coordinate_click_settles_before_press_and_keeps_a_visible_usb_pulse(interaction, monkeypatch):
    phases = []
    original_button = interaction.client.button

    class Settle:
        def is_set(self):
            return False

        def wait(self, timeout):
            phases.append(("settle", timeout))

    def button(name, down):
        phases.append(("button", down))
        original_button(name, down)

    monkeypatch.setattr(interaction, "_suspended", Settle())
    monkeypatch.setattr(interaction.client, "button", button)
    monkeypatch.setattr("dualpc.interaction.time.sleep", lambda seconds: phases.append(("sleep", seconds)))
    interaction.click(1720, 510, down_time=0.01)
    assert phases[0] == ("settle", interaction.settings.home_wait)
    assert phases[-4:] == [
                      ("settle", interaction.UI_MOVE_SETTLE), ("button", True),
                      ("sleep", interaction.UI_PRESS_MINIMUM), ("button", False)]
    assert all(phase == ("sleep", .008) for phase in phases[1:-4])


def test_stale_frame_rejects_output_and_releases_held_key(interaction):
    interaction.send_key_down("w")
    interaction.capture.fresh = False
    with pytest.raises(RuntimeError, match="stale"):
        interaction.send_key_down("e")
    time.sleep(0.12)
    assert not interaction.client.keys


def test_scroll_waits_for_game_to_process_pointer_move(interaction, monkeypatch):
    phases = []
    class Settle:
        def is_set(self):
            return False
        def wait(self, timeout):
            phases.append(("settle", timeout))
    monkeypatch.setattr(interaction, "_suspended", Settle())
    monkeypatch.setattr(interaction.client, "wheel", lambda amount: phases.append(("wheel", amount)))
    interaction.scroll(480, 700, -4)
    assert phases == [("settle", interaction.settings.home_wait),
                      ("settle", interaction.UI_MOVE_SETTLE), ("wheel", -4)]


def test_pause_releases_both_buttons_and_modifiers(interaction):
    executor = SimpleNamespace(paused=False, current_task=None)
    interaction.manager = SimpleNamespace(executor=executor)
    interaction.send_key_down("lshift")
    interaction.mouse_down(key="right")
    executor.paused = True
    time.sleep(0.12)
    assert interaction.client.modifiers == interaction.client.buttons == 0
    with pytest.raises(RuntimeError, match="paused"):
        interaction.send_key_down("q")


def test_input_text_rejects_unicode_before_sending(interaction):
    with pytest.raises(ValueError):
        interaction.input_text("test中文")
    assert not interaction.client.history


def test_ascii_shift_and_existing_key_hold(interaction):
    interaction.input_text("A! ")
    assert not interaction.client.keys and not interaction.client.modifiers
    interaction.send_key_down("w")
    interaction.send_key("w", down_time=0)
    assert interaction.key_code("w") in interaction.client.keys
    interaction.send_key_up("w")


def test_emergency_stop_latches(interaction):
    interaction.send_key_down("w")
    interaction.emergency_stop()
    assert not interaction.client.keys and not interaction.should_capture()
    with pytest.raises(RuntimeError, match="stopped"):
        interaction.send_key_down("w")




def test_kmbox_rehomes_after_game_recenters_without_using_cached_position(interaction, monkeypatch):
    position = [960, 540]
    original = interaction.client.move
    phases = []

    def move(dx, dy):
        original(dx, dy)
        position[:] = [min(1919, max(0, position[0] + dx)), min(1079, max(0, position[1] + dy))]
        phases.append((dx, dy))

    class Settle:
        def is_set(self):
            return False

        def wait(self, timeout):
            assert position == [0, 0]
            phases.append(("settle", timeout))

    monkeypatch.setattr(interaction.client, "move", move)
    monkeypatch.setattr(interaction, "_suspended", Settle())
    interaction.move(1200, 700)
    assert position == [1200, 700]
    position[:] = [960, 540]  # A menu transition recenters the real cursor.
    interaction.move(400, 250)
    assert position == [400, 250]
    assert sum(p[0] == "settle" for p in phases) == 2


def test_stop_during_homing_wait_does_not_send_target_movement(interaction, monkeypatch):
    interaction.settings.home_wait = 1
    homed = threading.Event()
    original = interaction.client.move

    def move(dx, dy):
        original(dx, dy)
        if dx < 0 and dy < 0:
            homed.set()

    monkeypatch.setattr(interaction.client, "move", move)
    errors = []

    def task():
        try:
            interaction.move(960, 540)
        except RuntimeError as error:
            errors.append(str(error))

    worker = threading.Thread(target=task)
    worker.start()
    assert homed.wait(1)
    interaction.suspend()
    worker.join(timeout=0.5)
    assert not worker.is_alive() and errors
    assert all(m[1] <= 0 and m[2] <= 0 for m in movements(interaction))


def test_menu_positioning_paces_small_reports_without_changing_total_or_drag(interaction, monkeypatch):
    sleeps = []
    monkeypatch.setattr('dualpc.interaction.time.sleep', sleeps.append)
    interaction.move(1720, 510)
    deltas = movements(interaction)[2:]
    assert all(max(abs(move[1]), abs(move[2])) <= 127 for move in deltas)
    assert sum(move[1] for move in deltas) == 1720
    assert sum(move[2] for move in deltas) == 510
    assert sleeps and min(sleeps) >= .008
