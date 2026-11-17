"""Workshop file operations for the C++ UI, using the original Python loader."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import sys
import traceback
import importlib


def initialize(root: Path, data: Path):
    os.chdir(root)
    sys.path[:0] = [str(root / 'vendor/ok-script'), str(root)]
    from ok.util.config import Config
    Config.config_folder = str(data / 'configs-dualpc')
    from dualpc.compat import apply_game_patch
    apply_game_patch(root)
    from dualpc.workshop_compat import install_workshop_compat
    install_workshop_compat()
    from src.char import CustomCharLoader as loader
    return loader


def command(loader, q):
    op, team = q['op'], q.get('team', [])
    classes = loader._registered_char_classes()
    if op == 'validate':
        checked = []
        groups = q.get('teams')
        if groups is None:
            groups = [team] if team else loader.list_custom_teams()
        for group in groups:
            if loader.team_code_state(group) == 'none':
                continue
            for member in loader.normalize_team(group):
                path = loader.get_team_import_file(group, member)
                active = loader.get_team_char_file(group, member)
                if loader.get_team_code_mode(group) == 'import' and (not active.is_file() or active.read_text(encoding='utf-8-sig') != path.read_text(encoding='utf-8-sig')):
                    raise ValueError(f'{member} 的当前代码与工坊备份不一致，请保存或重新切换后运行')
                loader._load_team_char_class_from_file(classes[member], group, path)
                checked.append(dict(team=list(group), member=member))
        return dict(valid=True, checked=checked)
    if op == 'mode':
        loader.switch_team_code_mode(team, q['mode'])
    elif op == 'all_mode':
        result = loader.switch_all_teams_code_mode(q['mode'])
        if result['failed'] or result['drifted']:
            raise ValueError('部分队伍代码有未保存差异或加载失败：' + str(result))
    elif op == 'save':
        loader.save_team_code_as_import(team, {q['member']: q['code']})
    elif op == 'import':
        loader.import_custom_team(loader.inspect_team_archive(q['path']))
    elif op == 'export':
        manifest = loader.read_team_manifest(team)
        destination = Path(q['path'])
        generated = loader.export_custom_team(team, destination.parent,
                         manifest.get('name') or 'Custom Team', manifest.get('description') or 'Custom team',
                         manifest.get('author') or 'User', manifest.get('version') or '1.0')
        if generated != destination:
            os.replace(generated, destination)
    else:
        raise ValueError('未知工坊文件操作：' + op)
    return dict(ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--persistent', action='store_true')
    a = parser.parse_args()
    output = sys.stdout
    output.reconfigure(encoding='utf-8')
    sys.stdout = sys.stderr
    sys.stderr.reconfigure(encoding='utf-8', errors='backslashreplace')
    loader = None
    while line := sys.stdin.buffer.readline(16 * 1024 * 1024):
        q = {}
        try:
            q = json.loads(line)
            if q['op'].startswith('library'):
                from workshop_sync import command as library_command
                result = library_command(a.data.absolute(), q, a.root.absolute())
            else:
                if loader is None:
                    loader = initialize(a.root.absolute(), a.data.absolute())
                else:
                    # Keep SDK imports warm, but reload user helpers and classes
                    # so edits never reuse an earlier successful validation.
                    data_root = a.data.resolve()
                    for name, module in list(sys.modules.items()):
                        path = getattr(module, '__file__', None)
                        if path and Path(path).resolve().is_relative_to(data_root):
                            sys.modules.pop(name, None)
                    importlib.invalidate_caches()
                    loader.clear_team_char_cache()
                result = command(loader, q)
            reply = dict(ok=True, data=result)
        except Exception as e:
            traceback.print_exc(file=sys.stderr)
            reply = dict(ok=False, error=str(e))
        if a.persistent:
            reply.update(request_id=q.get('request_id'), manager_protocol=1)
        output.write(json.dumps(reply, ensure_ascii=False, default=str) + '\n')
        output.flush()
        if not a.persistent:
            break


if __name__ == '__main__':
    main()
