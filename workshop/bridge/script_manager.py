"""File and metadata RPC for the native general-script editor."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
import traceback


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    args = parser.parse_args()
    sys.path[:0] = [str(args.root / 'vendor/ok-script'), str(args.root)]
    output = sys.stdout
    output.reconfigure(encoding='utf-8')
    sys.stdout = sys.stderr
    from scripts_store import ScriptStore
    store = ScriptStore(args.data)
    try:
        q = json.loads(sys.stdin.buffer.readline(16 * 1024 * 1024))
        op = q['op']
        ident = q.get('script', '')
        result = {}
        if op == 'create':
            result['selected'] = store.create(q['name'], q.get('copy_from'))
        elif op == 'save':
            store.save(ident, q['code'])
        elif op == 'record':
            import ast
            from ok.core.script_editing import merge_recorded_code
            recording = ast.parse(q['recording'])
            run = next(n for n in ast.walk(recording) if isinstance(n, ast.FunctionDef) and n.name == 'run')
            body = '\n'.join(ast.unparse(n) for n in run.body)
            store.save(ident, merge_recorded_code(store.get(ident)['source'], '', body))
        elif op == 'delete':
            store.remove(ident)
        elif op == 'import':
            store.import_package(q['path'])
        elif op == 'export':
            store.export(ident, q['path'])
        elif op == 'configure':
            store.configure(ident, q['key'], q['value'])
        elif op == 'reset':
            state = store.settings()
            state.setdefault(ident, {}).pop('values', None)
            store.save_settings(state)
        elif op == 'snippet':
            import ast
            from ok.core.script_templates import get_script_templates
            template = next(t for t in get_script_templates() if t['name'] == q['name'])
            arguments = []
            for name, default, doc in template['params']:
                value = str(q.get('values', {}).get(name, '')).strip()
                if not value:
                    if not default:
                        raise ValueError('请填写参数：' + name)
                    continue
                try:
                    parsed = ast.parse(value, mode='eval').body
                except SyntaxError:
                    parsed = None
                if parsed is None or isinstance(parsed, ast.Name) and parsed.id not in {'True', 'False', 'None'}:
                    value = repr(value)
                arguments.append((name + '=' if default else '') + value)
            result['snippet'] = 'self.' + template['name'] + '(' + ', '.join(arguments) + ')'
        elif op != 'catalog':
            raise ValueError('未知脚本操作：' + op)
        # Loading a task uses the original after_init/config hooks, including
        # computed defaults. This isolated process has no hardware ownership.
        if op in {'catalog', 'create', 'save', 'record', 'import'} and store.catalog():
            import subprocess
            worker = subprocess.run([sys.executable, str(Path(__file__).with_name('script_worker.py')),
                                     '--root', str(args.root.absolute()), '--data', str(args.data.absolute()),
                                     '--inspect'], capture_output=True, encoding='utf-8', timeout=45)
            inspected = json.loads(worker.stdout)
            if worker.returncode or not inspected.get('ok'):
                raise ValueError(inspected.get('error', '脚本配置加载失败，源文件已保留'))
            store.save_metadata(inspected['data']['entries'])
        result['scripts'] = store.catalog()
        from ok.core.script_templates import get_script_templates
        from ui_text import localize_templates
        result['templates'] = localize_templates(get_script_templates())
        from scripts_store import atomic_text
        atomic_text(store.data / 'native-script-catalog.json', json.dumps(dict(scripts=result['scripts'], templates=result['templates']), ensure_ascii=False, indent=2) + '\n')
        reply = dict(ok=True, data=result)
    except Exception as error:
        traceback.print_exc(file=sys.stderr)
        reply = dict(ok=False, error=str(error))
    output.write(json.dumps(reply, ensure_ascii=False, default=str) + '\n')
    output.flush()


if __name__ == '__main__':
    main()
