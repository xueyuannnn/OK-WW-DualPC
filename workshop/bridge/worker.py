"""Headless compatibility engine for the C++ desktop host.

Protocol: newline JSON on stdin/stdout; logs go to stderr. It does not launch
the old Qt window and cannot acquire video or send hardware reports itself.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import threading
import time
import traceback


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    root, data = Path(args.root).absolute(), Path(args.data).absolute()
    os.chdir(root)
    sys.path[:0] = [str(root / "vendor/ok-script"), str(root)]
    # Resource resolution in ok-script uses argv[0], not the bridge directory.
    sys.argv = [str(root / "main_dualpc.py"), "--headless"]
    output = sys.stdout
    output.reconfigure(encoding="utf-8", errors="backslashreplace")
    sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
    sys.stdout = sys.stderr
    sys.stdin.reconfigure(encoding="utf-8")
    output_lock = threading.Lock()

    def emit(value):
        with output_lock:
            output.write(json.dumps(value, ensure_ascii=False, default=str) + "\n")
            output.flush()

    from native_io import NativeChannel, install_native_io
    channel = NativeChannel()
    channel.request("heartbeat")
    install_native_io(channel)
    from dualpc.compat import apply_game_patch
    apply_game_patch(root)
    from dualpc.settings import Settings
    settings = Settings.load(data / "dualpc.json")
    import dualpc.bootstrap as bootstrap
    original_config = bootstrap.make_config

    def native_config(settings, root):
        config = original_config(settings, root)
        config.pop("gui", None)
        config.update(use_gui=False, locale="zh_CN", config_folder=str(data / "configs-dualpc"),
                      dualpc_settings_path=str(data / "dualpc.json"), custom_tabs=[], use_overlay=False)
        config.update(log_file=str(data / "logs/engine.log"), error_log_file=str(data / "logs/engine-error.log"),
                      screenshots_folder=str(data / "screenshots"))
        config["windows"]["start_exe"] = False
        return config

    bootstrap.make_config = native_config
    runtime = bootstrap.create_runtime(settings, root)
    executor = runtime.task_executor
    app = runtime.headless_app
    from ok.core.config_schema import build_config_fields
    from src.char import CustomCharLoader as characters
    stop = threading.Event()

    native_labels = {
        "Basic Options": "基本设置", "Notification": "通知",
        "Minimize Window to System Tray when Closing": "关闭窗口时最小化到托盘",
        "Use DirectML": "使用 DirectML 加速", "Trigger Interval": "实时任务检查间隔（毫秒）",
        "Start/Stop": "开始 / 暂停快捷键", "HotKey": "全局快捷键",
        "Use GPU to Improve Performance": "使用 GPU 加速识别",
        "Increase Delay between Trigger Tasks to Reduce CPU/GPU Usage(Milliseconds)": "提高间隔可降低 CPU / GPU 使用率。",
        "Game Package": "游戏画质资源包", "Yes": "是", "No": "否", "Auto": "自动", "None": "无",
    }

    def translate(value):
        return native_labels.get(value, app.tr(value))

    def fields(config, owner):
        result = build_config_fields(config, getattr(owner, "config_description", {}),
                                     getattr(owner, "config_type", {}), getattr(owner, "default_config", {}))
        if getattr(owner, "name", "") == "Basic Options":
            inactive = {"Auto Start Game When App Starts", "Mute Game while in Background", "Auto Resize Game Window",
                        "Exit App when Game Exits", "Kill Launcher After Start", "Launch with DX11", "Enable Blur",
                        "Blur Algorithm", "Blur Interval"}
            result = [f for f in result if f["key"] not in inactive]
        for field in result:
            field["label"] = translate(field["key"])
            field["description"] = translate(field["description"])
            if getattr(owner, "name", "") == "Game Hotkey":
                from dualpc.interaction import KEYS
                field.update(kind="select", options=list(dict.fromkeys([field["value"], *KEYS])))
            options = field.get("options")
            if isinstance(options, list):
                field["option_labels"] = [translate(str(value)) for value in options]
        return result

    def task_payload(task, trigger=False):
        return dict(id=task.__class__.__name__, name=app.tr(task.name),
                    description=app.tr(task.description), trigger=trigger,
                    enabled=task.enabled, paused=task.paused,
                    fields=fields(task.config, task))

    def teams():
        return [dict(team=list(team), name=characters.read_team_manifest(team).get("name", "/".join(team)),
                     mode=characters.team_code_state(team),
                     labels=[app.tr(characters.get_english_char_name(name)) for name in team])
                for team in characters.list_custom_teams()]

    def catalog():
        return dict(tasks=[task_payload(t, t in executor.trigger_tasks) for t in executor.get_all_tasks()],
                    globals=[dict(id=name, name=translate(name), fields=fields(conf, option))
                             for name, conf, option in runtime.global_config.get_all_visible_configs()
                             if option and name not in {"App Launcher"}], teams=teams(),
                    characters=[dict(id=name, name=app.tr(characters.get_english_char_name(name)))
                                for name in characters._registered_char_classes()],
                    native_preferences=dict(close_to_tray=runtime.global_config.get_config("Basic Options").get(
                        "Minimize Window to System Tray when Closing", False),
                        hotkey=runtime.global_config.get_config("Basic Options").get("Start/Stop", "F9")))

    def status():
        current = executor.current_task
        return dict(paused=executor.paused, current=app.tr(current.name) if current else "",
                    info=dict(getattr(current, "info", {}) or {}) if current else {},
                    tasks=[dict(id=t.__class__.__name__, enabled=t.enabled, paused=t.paused)
                           for t in executor.get_all_tasks()])

    def heartbeat():
        while not stop.wait(.25):
            try:
                state = channel.request("heartbeat")
                if not state.get("armed") and not executor.paused:
                    executor.pause()
                    runtime.device_manager.interaction.client.release_all()
                emit(dict(event="status", data=status()))
            except Exception as error:
                executor.pause()
                emit(dict(event="error", error=str(error)))
                stop.set()

    thread = threading.Thread(target=heartbeat, name="NativeHeartbeat", daemon=True)
    thread.start()

    def require_paused():
        if not executor.paused:
            raise RuntimeError("请先暂停任务，再修改角色代码或设置")

    def set_field(conf, owner, key, value):
        specs = {f["key"]: f for f in fields(conf, owner)}
        if key not in specs:
            raise ValueError("此配置项不存在或当前不可用")
        spec = specs[key]
        kind = spec["kind"]
        if kind == "boolean" and type(value) is not bool:
            raise ValueError("需要开关值")
        if kind in {"integer", "number"}:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("需要数值")
            if kind == "integer" and int(value) != value:
                raise ValueError("需要整数")
            if spec["minimum"] is not None and value < spec["minimum"]:
                raise ValueError("数值低于下限")
            if spec["maximum"] is not None and value > spec["maximum"]:
                raise ValueError("数值超过上限")
        if kind == "select" and spec.get("options") is not None and value not in spec["options"]:
            raise ValueError("请选择有效选项")
        conf[key] = value
        if conf.get(key) != value:
            raise ValueError("配置校验未通过")
        filename = getattr(conf, "config_file", None)
        if filename and json.loads(Path(filename).read_text(encoding="utf-8"))[key] != value:
            raise RuntimeError("配置写入磁盘失败")
        return catalog()

    def command(q):
        op = q["op"]
        if op == "catalog":
            return catalog()
        if op == "pause":
            executor.pause()
            runtime.device_manager.interaction.client.release_all()
            return status()
        if op == "start":
            task = runtime.get_task(q["task"])[0] if q.get("task") else None
            if not app.start_controller.do_start(task):
                raise RuntimeError("任务启动失败，请检查原生设备状态与日志")
            return status()
        if op == "stop_task":
            task = runtime.get_task(q["task"])[0]
            task.disable()
            task.unpause()
            return status()
        if op == "trigger":
            task, is_trigger = runtime.get_task(q["task"])
            if not is_trigger:
                raise ValueError("该任务不是实时触发任务")
            task.enable() if q["enabled"] else task.disable()
            return status()
        if op == "task_config":
            task = runtime.get_task(q["task"])[0]
            return set_field(task.config, task, q["key"], q["value"])
        if op == "global_config":
            gc = runtime.global_config
            return set_field(gc.get_config(q["group"]), gc.config_options[q["group"]], q["key"], q["value"])
        if op == "team_code":
            classes = characters._registered_char_classes()
            return dict(code=characters.read_team_char_code(q["team"], classes[q["member"]]))
        if op in {"team_mode", "team_save", "team_import", "team_create", "team_export"}:
            require_paused()
            if op == "team_mode":
                characters.switch_team_code_mode(q["team"], q["mode"])
            elif op == "team_save":
                cls = characters._registered_char_classes()[q["member"]]
                characters.save_team_char_code(q["team"], cls, q["code"])
            elif op == "team_import":
                characters.import_custom_team(characters.inspect_team_archive(q["path"]))
            elif op == "team_create":
                characters.create_custom_team(q["team"])
            elif op == "team_export":
                manifest = characters.read_team_manifest(q["team"])
                characters.export_custom_team(q["team"], q["path"], manifest.get("name") or "Custom Team",
                                              manifest.get("description") or "Custom team", manifest.get("author") or "User",
                                              manifest.get("version") or "1.0")
            characters.clear_team_char_cache()
            executor.reset_scene(check_enabled=False)
            return catalog()
        if op == "shutdown":
            stop.set()
            return {}
        raise ValueError(f"未知操作：{op}")

    try:
        info = catalog()
        emit(dict(event="ready", data=info))
        if args.self_test:
            assert all(t["name"] for t in info["tasks"])
            manager = runtime.device_manager
            manager.do_refresh(True)
            frame = manager.capture_method.reader.get_frame()
            assert frame is not None and frame.shape[:2] == (settings.capture.output_height, settings.capture.output_width)
            for _ in range(100):
                if channel.request("state").get("armed"):
                    break
                time.sleep(.01)
            interaction = manager.interaction
            client = interaction.client
            executor.paused = False
            interaction.send_key_down("a")
            assert 4 in channel.request("state")["box"]["keys"]
            interaction.send_key_up("a")
            interaction.mouse_down()
            assert channel.request("state")["box"]["buttons"] == 1
            time.sleep(.05)
            interaction.mouse_up()
            interaction.click(960, 540)
            interaction.send_key("e", down_time=.02)
            executor.paused = True
            client.release_all()
            assert not client.buttons and not client.keys
            imported = 0
            classes = characters._registered_char_classes()
            for team in characters.list_custom_teams():
                for member in team:
                    path = characters.get_team_import_file(team, member)
                    if path.is_file():
                        characters._load_team_char_class_from_file(classes[member], team, path)
                        imported += 1
            emit(dict(event="self_test", data=dict(ok=True, tasks=len(info["tasks"]), teams=len(info["teams"]),
                                                    frame=list(frame.shape), native_transport=True,
                                                    imported_classes=imported, interaction_roundtrip=True)))
        else:
            for line in sys.stdin:
                q = {}
                try:
                    q = json.loads(line)
                    result = command(q)
                    emit(dict(id=q.get("id"), ok=True, data=result))
                except Exception as error:
                    traceback.print_exc(file=sys.stderr)
                    emit(dict(id=q.get("id") if isinstance(q, dict) else None, ok=False, error=str(error)))
                if stop.is_set():
                    break
    finally:
        stop.set()
        thread.join(timeout=3)
        runtime.quit()
        runtime.device_manager.close()
        channel.close()


if __name__ == "__main__":
    main()
