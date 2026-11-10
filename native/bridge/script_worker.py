"""Execute original general scripts; C++ owns capture, cancellation and HID."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import sys
import threading
import time
import traceback


def create_runtime(root, data, channel):
    sys.path[:0] = [str(root / 'vendor/ok-script'), str(root)]
    sys.argv = [str(root / 'main_dualpc.py'), '--headless']
    from native_io import install_native_io
    install_native_io(channel, direct_input=True)
    from dualpc.compat import apply_game_patch
    apply_game_patch(root)
    from dualpc.settings import Settings
    import dualpc.bootstrap as bootstrap
    settings = Settings.load(data / 'dualpc.json') if (data / 'dualpc.json').is_file() else Settings()
    settings.capture.crop = None  # already applied by the native frame source
    original = bootstrap.make_config

    def configure(settings, root):
        config = original(settings, root)
        config.pop('gui', None)
        config.update(use_gui=False, locale='zh_CN', custom_tabs=[], use_overlay=False,
                      config_folder=str(data / 'configs-dualpc'), custom_tasks=True,
                      dualpc_settings_path=str(data / 'dualpc.json'),
                      log_file=str(data / 'logs/scripts.log'), error_log_file=str(data / 'logs/scripts-error.log'),
                      screenshots_folder=str(data / 'screenshots'))
        config['windows']['start_exe'] = False
        config['template_matching'] = dict(config['template_matching'],
                                           coco_feature_json=str(root / 'assets/coco_annotations.json'))
        return config

    bootstrap.make_config = configure
    # The original loader discovers ok_tasks/ok_import relative to cwd. Keep all
    # user writes with the profile, never beside the portable compatibility SDK.
    os.chdir(data)
    runtime = bootstrap.create_runtime(settings, root)
    executor = runtime.task_executor
    executor.thread = threading.current_thread()
    executor.paused = False
    manager = runtime.device_manager
    manager.capture_method.reader._closed = False
    manager.interaction.client.connected = True
    channel.request('state')
    manager.interaction.resume_connection()
    manager._describe()
    return runtime


def inspect_runtime(runtime, data):
    from ok import og, TriggerTask
    from scripts_store import ScriptStore, fields
    store = ScriptStore(data)
    manager = og.task_manager
    entries = []
    for entry in store.catalog():
        path = store.resolve(entry['path'])
        task = next((task for task, value in manager.task_map.items() if Path(value[0]).resolve() == path), None)
        error = next((error for file, error in manager.task_errors.items() if Path(file).resolve() == path), '')
        metadata = dict(error=error, native_supported=task is not None and not error)
        if task is not None:
            metadata.update(name=task.name, description=task.description, trigger=isinstance(task, TriggerTask),
                            interval=max(.05, task.trigger_interval),
                            fields=fields(dict(task.config), task.config_description, task.config_type))
        entries.append(dict(path=entry['path'], metadata=metadata))
    return dict(entries=entries)


class InspectionChannel:
    """The original loader may instantiate classes, but cannot open devices."""
    state = dict(capture_connected=False, armed=False, box=dict(connected=False, keyboard=False), actual={})

    def request(self, op, **values):
        if op in {'state', 'heartbeat', 'release'}:
            return self.state
        raise RuntimeError('加载脚本配置时不采集或发送键鼠，请在运行方法中执行设备操作')

    def fresh(self):
        return False

    def frame(self):
        raise RuntimeError('加载脚本配置时没有采集画面')


def run_script(runtime, data, request, channel):
    from scripts_store import ScriptStore
    from ok.task.exceptions import FinishedException
    entry = ScriptStore(data).get(request['script'])
    target = (data / entry['path']).resolve()
    from ok import og
    task_manager = og.task_manager
    task = next((task for task, value in task_manager.task_map.items()
                 if Path(value[0]).resolve() == target), None)
    if task is None:
        error = next((error for path, error in task_manager.task_errors.items()
                      if Path(path).resolve() == target), '脚本没有可运行的 BaseTask/TriggerTask 子类')
        raise ValueError(error)
    from ok import TriggerTask
    # Trigger predicates and run() must observe the same iteration's settings.
    for key, value in request.get('values', {}).items():
        task.config[key] = value
    if isinstance(task, TriggerTask) and not task.should_trigger():
        return dict(handled=False)
    task._enabled = True
    task.unpause()
    task.running = True
    executor = runtime.task_executor
    executor.current_task = task
    original_info = task.info_set
    def info(key, value):
        original_info(key, value)
        channel.request('script_info', key=str(key), value=str(value))
    task.info_set = info
    original_run_task = task.run_task_by_class
    def run_task(cls):
        if cls.__module__.startswith(('src.task.', 'dualpc.task_hooks')):
            channel.request('script_builtin', task=cls.__name__)
        else:
            original_run_task(cls)
    task.run_task_by_class = run_task
    handled = False
    try:
        executor.reset_scene(check_enabled=False)
        executor.interaction.on_run()
        executor.next_frame(time_out=4)
        task.start_time = time.time()
        # There is no Python task-executor loop. Only this selected extension
        # runs; C++ keeps ownership of trigger ordering, repeat and pause.
        handled = bool(task.run())
    except FinishedException:
        pass
    finally:
        task.info_set = original_info
        task.run_task_by_class = original_run_task
        task.running = False
        task._enabled = False
        executor.current_task = None
        runtime.device_manager.interaction.client.release_all()
    return dict(handled=handled, name=task.name, description=task.description,
                trigger_interval=task.trigger_interval,
                fields=__import__('scripts_store').fields(dict(task.config), task.config_description, task.config_type))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--data', type=Path, required=True)
    p.add_argument('--mapping')
    p.add_argument('--inspect', action='store_true')
    a = p.parse_args()
    root, data = a.root.absolute(), a.data.absolute()
    output, incoming = sys.stdout, sys.stdin
    output.reconfigure(encoding='utf-8')
    incoming.reconfigure(encoding='utf-8')
    sys.stdout = sys.stderr
    sys.stderr.reconfigure(encoding='utf-8', errors='backslashreplace')
    if a.inspect:
        runtime = create_runtime(root, data, InspectionChannel())
        output.write(json.dumps(dict(ok=True, data=inspect_runtime(runtime, data)), ensure_ascii=False, default=str) + '\n')
        output.flush()
        return
    if not a.mapping:
        raise ValueError('缺少原生共享图像通道')
    from workshop_worker import PipeChannel
    channel = PipeChannel(output, incoming, a.mapping)
    runtime = create_runtime(root, data, channel)
    output.write(json.dumps(dict(event='ready')) + '\n')
    output.flush()
    for line in incoming:
        try:
            request = json.loads(line)
            result = run_script(runtime, data, request, channel)
            reply = dict(event='reply', ok=True, data=result)
        except Exception as error:
            traceback.print_exc(file=sys.stderr)
            reply = dict(event='reply', ok=False, error=str(error))
        output.write(json.dumps(reply, ensure_ascii=False, default=str) + '\n')
        output.flush()


if __name__ == '__main__':
    try:
        main()
        # The original SDK owns background workers. This short-lived process
        # has already flushed its reply; do not wait forever for their queues.
        os._exit(0)
    except BaseException as error:
        traceback.print_exc(file=sys.stderr)
        sys.__stdout__.write(json.dumps(dict(event='fatal', ok=False, error=str(error)), ensure_ascii=False) + '\n')
        sys.__stdout__.flush()
        os._exit(1)
