"""The host owns common combat state; custom rotation attributes stay in Python."""
from __future__ import annotations

import math
import inspect
import time

PROTOCOL_VERSION = 2
API_VERSION = 1
CAPABILITIES = ('request-identity', 'combat-state-ages', 'immutable-frame-cache', 'bridge-metrics',
                'character-policy', 'task-config-context')
TIMES = {
    'last_res': 'last_res', 'last_echo': 'last_echo', 'last_liberation': 'last_liberation',
    'last_switch_in': 'last_switch_in_time', 'last_outro': 'last_outro_time',
    'last_switch': 'last_switch_time', 'last_full_con_switch': 'last_full_con_switch_time',
    'last_buff': 'last_buff_time', 'last_perform': 'last_perform',
}


def number(value):
    if not isinstance(value, (float, int)) or isinstance(value, bool) or not math.isfinite(value):
        raise ValueError('工坊状态包含无效数值')
    return float(value)


def boolean(value):
    if type(value) is not bool:
        raise ValueError('工坊状态包含无效开关值')
    return value


def export_state(task, now=None):
    now = time.time() if now is None else now
    chars = []
    for c in task.chars:
        values = dict(index=c.index, current=bool(c.is_current_char), intro=bool(c.has_intro),
                      sub_intro=bool(c.has_sub_dps_intro), role=str(c.char_type),
                      current_con=number(c.current_con), check_f=bool(c.check_f_on_switch))
        values['healer'] = bool(getattr(c, 'is_healer', c.char_type == 'Healer'))
        values['short_target'] = bool(getattr(c, 'target_box_short_combat_check', False))
        values['buff_time'] = number(c.buff_time)
        for key, attr in TIMES.items():
            stamp = number(getattr(c, attr))
            values[key] = -1 if stamp < 0 else now - stamp
        chars.append(values)
    return dict(chars=chars, freezes=[dict(age=now - start, duration=duration, threshold=threshold)
                for start, duration, threshold in task.freeze_durations],
                in_liberation=bool(task.in_liberation),
                combat_age=-1 if task.combat_start < 0 else now-task.combat_start)


def apply_state(task, state, now=None):
    now = time.time() if now is None else now
    chars = state['chars']
    expected = {c.index for c in task.chars}
    if any(type(v['index']) is not int for v in chars):
        raise ValueError('工坊角色位置无效')
    if len(chars) != len(expected) or {v['index'] for v in chars} != expected:
        raise ValueError('工坊队伍状态与当前队伍不一致')
    prepared = []
    for value in chars:
        for flag in ('current', 'intro', 'sub_intro', 'check_f'):
            boolean(value[flag])
        times = {attr: -1 if number(value[key]) < 0 else now-number(value[key])
                 for key, attr in TIMES.items()}
        if value['role'] not in {'MainDps', 'SubDps', 'Healer'}:
            raise ValueError('工坊角色定位无效')
        con = number(value['current_con'])
        if not 0 <= con <= 1:
            raise ValueError('工坊协奏值无效')
        buff = number(value['buff_time'])
        if not 0 <= buff <= 86400:
            raise ValueError('工坊角色增益时间无效')
        prepared.append((value, times, buff))
    freezes = [(now-number(v['age']), number(v['duration']), number(v['threshold']))
               for v in state.get('freezes', [])]
    if len(freezes) > 2048 or any(duration < 0 or duration > 600 for _, duration, _ in freezes):
        raise ValueError('工坊冻结时间无效')
    combat_age = number(state.get('combat_age', -1))
    in_liberation = boolean(state['in_liberation'])
    by_index = {c.index: c for c in task.chars}
    for value, times, buff in prepared:
        c = by_index[value['index']]
        for attr, stamp in times.items():
            setattr(c, attr, stamp)
        c.is_current_char = bool(value['current'])
        c.has_intro, c.has_sub_dps_intro = bool(value['intro']), bool(value['sub_intro'])
        c.current_con = value['current_con']
        c.check_f_on_switch = bool(value['check_f'])
        c.set_char_type(value['role'])
        # Keep author-defined getters/properties intact. Updating the underlying
        # configuration preserves the same BaseChar contract as set_char_type.
        c.set_buff_time(buff)
    task.freeze_durations = freezes
    task.in_liberation = in_liberation
    task.combat_start = -1 if combat_age < 0 else now-combat_age


def end_current_character(task):
    current = next((c for c in task.chars if c.is_current_char), None)
    if current:
        current.on_combat_end(task.chars)


def switch_priority(actor, current, intro, target_low_con=False):
    method = actor.get_switch_priority
    # Some older workshop classes implement the two-argument public method.
    # Inspect the signature rather than catching a TypeError raised by the author.
    signature = inspect.signature(method)
    try:
        signature.bind(current, intro, target_low_con)
    except TypeError:
        signature.bind(current, intro)
        return int(method(current, intro))
    return int(method(current, intro, target_low_con))


def validate_request(q, session, previous_id=0):
    if q.get('protocol') != PROTOCOL_VERSION or q.get('api') != API_VERSION:
        raise ValueError('工坊桥接版本不一致，请使用同一完整包中的主程序和兼容引擎')
    if not q.get('session') or (session and q['session'] != session) or not q.get('request_id'):
        raise ValueError('工坊会话或请求标识不一致')
    request_id = q['request_id']
    if not isinstance(request_id, str) or not request_id.isascii() or not request_id.isdigit() or len(request_id) > 20:
        raise ValueError('工坊请求序号无效')
    if int(request_id) <= previous_id:
        raise ValueError('工坊请求已过期')
    return q['session']
