import copy
import sys
import threading
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'bridge'))
from workshop_protocol import (TIMES, apply_state, export_state, end_current_character,
                               validate_request)
from workshop_worker import PipeChannel


def task():
    chars = []
    for i in range(3):
        c = SimpleNamespace(index=i, is_current_char=i == 0, has_intro=i == 0,
                            has_sub_dps_intro=False, char_type='MainDps', current_con=.5,
                            check_f_on_switch=True, custom_rotation={'stage': 3})
        c.set_char_type = lambda role, c=c: setattr(c, 'char_type', role)
        for attr in TIMES.values():
            setattr(c, attr, 990 if i == 0 else -1)
        chars.append(c)
    return SimpleNamespace(chars=chars, freeze_durations=[(995, 1.2, -100)],
                           in_liberation=False, combat_start=980)


def test_common_state_preserves_ages_across_clock_domains_and_custom_state():
    original = task()
    state = export_state(original, now=1000)
    other = task()
    apply_state(other, state, now=2000)
    assert other.chars[0].last_buff_time == 1990
    assert other.chars[0].last_full_con_switch_time == 1990
    assert other.freeze_durations == [(1995, 1.2, -100)]
    assert export_state(other, now=2000) == state
    assert other.chars[0].custom_rotation == {'stage': 3}


@pytest.mark.parametrize('damage', ['duplicate', 'nan', 'role', 'freeze'])
def test_invalid_snapshot_is_rejected_before_mutating_any_character(damage):
    owner = task()
    before = export_state(owner, now=1000)
    state = copy.deepcopy(before)
    if damage == 'duplicate':
        state['chars'][1]['index'] = 0
    elif damage == 'nan':
        state['chars'][1]['last_buff'] = float('nan')
    elif damage == 'role':
        state['chars'][1]['role'] = 'unsupported'
    else:
        state['freezes'][0]['duration'] = -2
    with pytest.raises(ValueError):
        apply_state(owner, state, now=1000)
    assert export_state(owner, now=1000) == before


def test_only_current_callback_runs_with_actual_cantarella_and_augusta_implementations():
    from src.char.Cantarella import Cantarella
    from src.char.Augusta import Augusta
    send = Mock()
    owner = SimpleNamespace(send_key=send)
    current = SimpleNamespace(is_current_char=True, on_combat_end=lambda _: None)
    cantarella, augusta = object.__new__(Cantarella), object.__new__(Augusta)
    for index, c in enumerate((cantarella, augusta), 1):
        c.index, c.task, c.logger, c.is_current_char = index, owner, Mock(), False
    owner.chars = [current, cantarella, augusta]
    end_current_character(owner)
    send.assert_not_called()
    current.is_current_char, cantarella.is_current_char = False, True
    end_current_character(owner)
    send.assert_called_once_with('3')


@pytest.mark.parametrize('change', [{'protocol': 1}, {'api': 9}, {'session': 'old'},
                                    {'request_id': '1'}, {'request_id': 'invalid'}])
def test_requests_reject_old_sessions_replays_and_protocol_mismatch(change):
    q = dict(protocol=2, api=1, session='current', request_id='2') | change
    with pytest.raises(ValueError):
        validate_request(q, 'current', previous_id=1)


def test_repeated_frame_uses_owned_immutable_bytes_and_new_frame_does_not_mutate_previous():
    class Memory:
        data = b'original'
        copies = 0
        def __len__(self): return len(self.data)
        def __getitem__(self, key):
            self.copies += 1
            return self.data[key]
    channel = object.__new__(PipeChannel)
    channel.lock, channel.memory = threading.RLock(), Memory()
    channel.cached_meta = channel.cached_frame = None
    meta = dict(available=True, size=8, sequence='1', width=8, height=1, stride=8, format='RGB3')
    channel.request = lambda *_: meta
    _, first = channel.frame()
    _, second = channel.frame()
    assert first is second and channel.memory.copies == 1
    meta['sequence'], channel.memory.data = '2', b'updated!'
    _, third = channel.frame()
    assert first == b'original' and third == b'updated!' and channel.memory.copies == 2
