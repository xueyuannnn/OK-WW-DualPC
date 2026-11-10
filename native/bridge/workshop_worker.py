"""Run workshop characters only. Capture, HID ownership and task scheduling stay in C++."""
from __future__ import annotations
import argparse
import json
import mmap
import os
from pathlib import Path
import sys
import threading
import time
import traceback
from workshop_protocol import (PROTOCOL_VERSION, API_VERSION, CAPABILITIES,
                               export_state, apply_state, validate_request, end_current_character)


class PipeChannel:
    def __init__(self, output, incoming, mapping):
        self.output, self.incoming = output, incoming
        self.lock = threading.RLock()
        self.memory = mmap.mmap(-1, 64 * 1024 * 1024, tagname=mapping, access=mmap.ACCESS_READ)
        self.state = {}
        self.session = self.request_id = ''
        self.last_request_id = 0
        self.cached_meta = self.cached_frame = None

    def request(self, op, **values):
        with self.lock:
            self.output.write(json.dumps(dict(event='rpc', session=self.session, request_id=self.request_id,
                                             request=dict(op=op, **values)), separators=(',', ':')) + '\n')
            self.output.flush()
            reply = json.loads(self.incoming.readline())
            if not reply.get('ok'):
                raise RuntimeError(reply.get('error', 'C++ 工坊通道已停止'))
            result = reply['data']
            if op in {'state', 'heartbeat', 'connect', 'probe'}:
                self.state = result
            return result

    def frame(self):
        with self.lock:
            meta = self.request('frame')
            if not meta.get('available'):
                return None, None
            size = meta['size']
            if not 0 < size <= len(self.memory):
                raise ValueError('工坊图像尺寸无效')
            key = tuple(meta.get(k) for k in ('sequence', 'width', 'height', 'stride', 'format', 'size'))
            if key != self.cached_meta:
                self.cached_frame = self.memory[:size]
                self.cached_meta = key
            return meta, self.cached_frame

    def fresh(self):
        return True  # C++ checks the live execution epoch and device health on every RPC.


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--data', type=Path, required=True)
    p.add_argument('--mapping', required=True)
    a = p.parse_args()
    root, data = a.root.absolute(), a.data.absolute()
    os.chdir(root)
    sys.path[:0] = [str(root/'vendor/ok-script'), str(root)]
    sys.argv = [str(root/'main_dualpc.py'), '--headless']
    output, incoming = sys.stdout, sys.stdin
    output.reconfigure(encoding='utf-8')
    incoming.reconfigure(encoding='utf-8')
    sys.stdout = sys.stderr
    sys.stderr.reconfigure(encoding='utf-8', errors='backslashreplace')
    channel = PipeChannel(output, incoming, a.mapping)
    from native_io import install_native_io
    install_native_io(channel)
    from dualpc.compat import apply_game_patch
    apply_game_patch(root)
    from dualpc.settings import Settings
    import dualpc.bootstrap as bootstrap
    settings = Settings.load(data/'dualpc.json')
    # C++ already applies the configured crop and inference resolution.
    settings.capture.crop = None
    original_config = bootstrap.make_config
    def worker_config(settings, root):
        config = original_config(settings, root)
        config.pop('gui', None)
        config.update(use_gui=False, locale='zh_CN', config_folder=str(data/'configs-dualpc'),
                      custom_tabs=[], use_overlay=False, dualpc_settings_path=str(data/'dualpc.json'),
                      log_file=str(data/'logs/workshop.log'), error_log_file=str(data/'logs/workshop-error.log'),
                      screenshots_folder=str(data/'screenshots'))
        config['windows']['start_exe'] = False
        return config
    bootstrap.make_config = worker_config
    runtime = bootstrap.create_runtime(settings, root)
    executor = runtime.task_executor
    task = runtime.get_task('AutoCombatTask')[0]
    # No executor loop is started. C++ calls one character segment at a time.
    executor.thread = threading.current_thread()
    executor.current_task = task
    executor.paused = False
    task._enabled = True
    task.unpause()
    manager = runtime.device_manager
    manager.capture_method.reader._closed = False
    manager.interaction.client.connected = True
    channel.request('state')
    manager.interaction.resume_connection()
    manager._describe()
    from src.char import CustomCharLoader as loader
    from src.char.CharFactory import char_dict, _apply_char_config
    from src.char.BaseChar import CharType
    from src.task.BaseCombatTask import BaseCombatTask, NotInCombatException, CharDeadException
    classes = loader._registered_char_classes()
    team = []
    ended = True

    def bind_task(task_id):
        nonlocal task
        actual = executor.get_task_by_class_name(task_id)
        if not isinstance(actual, BaseCombatTask):
            raise ValueError('当前任务没有可兼容的工坊战斗上下文：' + task_id)
        if actual is not task:
            actual.chars = task.chars
            for c in actual.chars:
                if c:
                    c.task = actual
            task = actual
        if getattr(task, 'host_task_id', None) != task_id or executor.current_task is not task:
            executor.current_task = task
            task.host_task_id = task_id
            task._enabled = True
            task.unpause()

    def state():
        return export_state(task)

    def configure(q):
        nonlocal team
        party = q['party']
        team = [c['class'] for c in party]
        task.chars = []
        for member in party:
            name = member['class']
            cls = loader._load_team_char_class_from_file(classes[name], team,
                       loader.get_custom_team_folder(team)/f'{name}.py')
            char = cls(task, member['index'], char_name=member['canonical'], confidence=1,
                       ring_index=member['element'], char_type=CharType(member['role']), buff_time=member['buff'])
            info = char_dict.get(member['canonical'])
            task.chars.append(_apply_char_config(task, char, info) if info else char)
        shared = q['host_state']
        for value in shared['chars']:
            value['role'] = str(next(c for c in task.chars if c.index == value['index']).char_type)
        apply_state(task, shared)
        return state()

    def command(q):
        nonlocal ended
        op = q['op']
        bind_task(q['task'])
        if op == 'load':
            return configure(q)
        if op == 'ping':
            return {}
        apply_state(task, q['host_state'])
        if op == 'reset':
            ended = False
            task._in_combat = True
            task.combat_start = time.time()
            task.freeze_durations = []
            task.skip_combat_check = False
            executor.reset_scene(check_enabled=False)
            for c in task.chars:
                c.reset_state()
            return state()
        if op == 'end':
            try:
                if not ended:
                    ended = True
                    end_current_character(task)
                    for c in task.chars:
                        c.reset_state()
            finally:
                task._in_combat = False
                manager.interaction.client.release_all()
            return state()
        if op in {'perform', 'priority'}:
            task.use_liberation = q.get('use_liberation', True)
            if q.get('f_rearm', -1) >= 0:
                task._f_last_press = time.time() - q['f_age']
            index = q['index']
            for c in task.chars:
                c.is_current_char = c.index == q.get('current', index)
            actor = task.chars[index]
            if op == 'priority':
                return dict(priority=int(actor.get_switch_priority(task.get_current_char(), q.get('intro', False))))
            executor.reset_scene(check_enabled=False)
            task.load_hotkey()
            try:
                actor.perform()
            except CharDeadException as e:
                return dict(**state(), interrupted=str(e), dead=True)
            except NotInCombatException as e:
                return dict(**state(), interrupted=str(e))
            finally:
                manager.interaction.client.release_all()
            return state()
        raise ValueError('未知工坊执行操作：' + op)

    output.write(json.dumps(dict(event='ready', protocol=PROTOCOL_VERSION, api=API_VERSION,
                                capabilities=CAPABILITIES, python=sys.version.split()[0]))+'\n')
    output.flush()
    for line in incoming:
        try:
            q = json.loads(line)
            channel.session = validate_request(q, channel.session, channel.last_request_id)
            channel.request_id = q['request_id']
            channel.last_request_id = int(q['request_id'])
            result = command(q)
            reply = dict(event='reply', ok=True, data=result, session=channel.session,
                         request_id=channel.request_id)
        except Exception as error:
            traceback.print_exc(file=sys.stderr)
            reply = dict(event='reply', ok=False, error=str(error), session=channel.session,
                         request_id=channel.request_id)
        output.write(json.dumps(reply, ensure_ascii=False, default=str)+'\n')
        output.flush()


if __name__ == '__main__':
    try:
        main()
    except BaseException as error:
        traceback.print_exc(file=sys.stderr)
        sys.__stdout__.write(json.dumps(dict(event='fatal', ok=False, error=str(error)), ensure_ascii=False)+'\n')
        sys.__stdout__.flush()
        os._exit(1)
