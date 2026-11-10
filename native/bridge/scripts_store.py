"""Profile-owned general Python tasks and original .okscript packages.

Listing and editing only parse syntax. User code executes in script_worker,
where capture and input are provided by the native host.
"""
from __future__ import annotations

import ast
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import uuid


def atomic_text(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix='.script-', dir=path.parent)
    try:
        with os.fdopen(handle, 'w', encoding='utf-8', newline='\n') as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def script_id(relative: str):
    return 'script:' + hashlib.sha256(relative.encode()).hexdigest()[:24]


def fields(defaults, descriptions=None, types=None):
    result = []
    for key, value in defaults.items():
        if key.startswith('_'):
            continue
        kind = ('boolean' if isinstance(value, bool) else 'integer' if isinstance(value, int)
                else 'number' if isinstance(value, float) else 'list' if isinstance(value, list)
                else 'text' if isinstance(value, str) else 'json')
        if isinstance(value, list) and any(isinstance(item, (list, dict)) for item in value):
            kind = 'json'
        spec = dict(key=key, label=key, value=value, kind=kind,
                    description=(descriptions or {}).get(key, ''))
        options = (types or {}).get(key)
        if isinstance(options, list):
            spec.update(kind='select', options=options)
        elif isinstance(options, dict):
            # Preserve the original UI metadata when it uses primitive values.
            spec.update({k: options[k] for k in ('minimum', 'maximum', 'hidden') if k in options})
        result.append(spec)
    return result


def source_metadata(path: Path):
    source = path.read_text(encoding='utf-8-sig')
    tree = ast.parse(source, filename=path.name)
    classes = [c for c in tree.body if isinstance(c, ast.ClassDef)]
    cls = next((c for c in classes if any(isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))
                                         and n.name == 'run' for n in c.body) or
                any('Task' in ast.unparse(base) for base in c.bases)), None)
    if cls is None:
        raise LookupError('没有找到任务类')
    values = {}
    for node in ast.walk(cls):
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Attribute) and isinstance(target.value, ast.Name) and target.value.id == 'self':
                    try:
                        values[target.attr] = ast.literal_eval(node.value)
                    except (ValueError, TypeError, SyntaxError):
                        pass
    trigger = any('Trigger' in ast.unparse(base) for base in cls.bases)
    return dict(name=values.get('name', cls.name), description=values.get('description', ''),
                class_name=cls.name, trigger=trigger, enabled=False,
                fields=fields(values.get('default_config', {}), values.get('config_description'),
                              values.get('config_type')))


class ScriptStore:
    def __init__(self, data):
        self.data = Path(data).resolve()

    def resolve(self, relative):
        relative = str(relative).replace('\\', '/')
        parts = relative.split('/')
        if (not parts or parts[0] not in {'ok_tasks', 'ok_import'} or
                any(p in {'', '.', '..'} or ':' in p for p in parts)):
            raise ValueError('脚本路径无效')
        path = (self.data / relative).resolve()
        if self.data not in path.parents:
            raise ValueError('脚本路径超出个人配置目录')
        return path

    def settings(self):
        file = self.data / 'native-scripts.json'
        return json.loads(file.read_text(encoding='utf-8')) if file.is_file() else {}

    def save_settings(self, settings):
        atomic_text(self.data / 'native-scripts.json', json.dumps(settings, ensure_ascii=False, indent=2) + '\n')

    def catalog(self):
        paths = sorted((self.data / 'ok_tasks').glob('*.py'))
        paths += sorted((self.data / 'ok_import').glob('*/*.py'))
        settings = self.settings()
        cache_path = self.data / 'native-script-metadata.json'
        cache = json.loads(cache_path.read_text(encoding='utf-8')) if cache_path.is_file() else {}
        result = []
        for path in paths:
            relative = path.relative_to(self.data).as_posix()
            self.resolve(relative)
            entry = dict(id=script_id(relative), path=relative, source=path.read_text(encoding='utf-8-sig'),
                         name=path.stem, description='', trigger=False, enabled=False, fields=[],
                         native_supported=True, custom_script=True, error='')
            try:
                entry.update(source_metadata(path))
                metadata = cache.get(relative, {})
                if metadata.get('sha256') == hashlib.sha256(path.read_bytes()).hexdigest():
                    entry.update(metadata.get('metadata', {}))
            except LookupError:
                continue  # enum and helper modules belong to the package, not the task list
            except (ValueError, SyntaxError, UnicodeError) as error:
                entry.update(error=str(error), native_supported=False)
            saved = settings.get(entry['id'], {})
            for field in entry['fields']:
                field['value'] = saved.get('values', {}).get(field['key'], field['value'])
            entry.update(enabled=bool(saved.get('enabled', False)), repeat=saved.get('repeat', 1),
                         continuous=bool(saved.get('continuous', False)), interval=saved.get('interval', .1))
            result.append(entry)
        return result

    def save_metadata(self, entries):
        cached = {}
        for entry in entries:
            path = self.resolve(entry['path'])
            cached[entry['path']] = dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                                         metadata=entry['metadata'])
        atomic_text(self.data/'native-script-metadata.json', json.dumps(cached, ensure_ascii=False, indent=2) + '\n')

    def get(self, ident):
        return next((s for s in self.catalog() if s['id'] == ident), None) or self._missing()

    @staticmethod
    def _missing():
        raise ValueError('脚本已不存在，请刷新列表')

    def configure(self, ident, key, value):
        self.get(ident)
        state = self.settings()
        saved = state.setdefault(ident, {})
        if key == '_enabled':
            if not isinstance(value, bool):
                raise ValueError('启用状态无效')
            saved['enabled'] = value
        elif key == '_repeat':
            if not isinstance(value, int) or isinstance(value, bool) or not 1 <= value <= 1000000:
                raise ValueError('循环次数应在 1 至 1000000 之间')
            saved['repeat'] = value
        elif key == '_continuous':
            if not isinstance(value, bool):
                raise ValueError('持续循环状态无效')
            saved['continuous'] = value
        else:
            saved.setdefault('values', {})[key] = value
        self.save_settings(state)

    def create(self, name, copy_from=None):
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,79}', name):
            raise ValueError('文件名请使用英文字母、数字和下划线，且不要以数字开头')
        path = self.resolve('ok_tasks/' + name + '.py')
        if path.exists():
            raise ValueError('同名脚本已存在')
        code = self.get(copy_from)['source'] if copy_from else (
            'from ok import BaseTask\n\n\nclass ' + name + '(BaseTask):\n'
            '    def __init__(self, *args, **kwargs):\n'
            '        super().__init__(*args, **kwargs)\n'
            '        self.name = "新脚本"\n'
            '        self.description = ""\n\n'
            '    def run(self):\n'
            '        self.log_info("脚本开始")\n'
            '        # 在这里编写操作，采集和键鼠由双机主程序提供。\n'
            '        self.sleep(0.5)\n')
        atomic_text(path, code)
        return script_id(path.relative_to(self.data).as_posix())

    def save(self, ident, code):
        entry = self.get(ident)
        ast.parse(code, filename=entry['path'])
        atomic_text(self.resolve(entry['path']), code)

    def remove(self, ident):
        entry = self.get(ident)
        source = self.resolve(entry['path'])
        target = self.data / 'deleted-scripts' / uuid.uuid4().hex / source.name
        target.parent.mkdir(parents=True)
        os.replace(source, target)
        settings = self.settings()
        settings.pop(ident, None)
        self.save_settings(settings)

    def export(self, ident, destination):
        from ok.core.script_packager import export_script
        import shutil
        entry = self.get(ident)
        folder = self.resolve(entry['path']).parent
        destination = Path(destination).absolute()
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Build in a temporary folder so the publisher's manifest is preserved.
        with tempfile.TemporaryDirectory(prefix='.export-', dir=self.data) as temporary:
            stage = Path(temporary) / 'script'
            shutil.copytree(folder, stage, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
            manifest = stage / 'manifest.json'
            old = json.loads(manifest.read_text(encoding='utf-8')) if manifest.exists() else {}
            package_id = old.get('file_name', folder.name if folder.name != 'ok_tasks' else 'custom_scripts')
            if not re.fullmatch(r'[A-Za-z0-9_.-]+', package_id) or package_id in {'.', '..'}:
                raise ValueError('脚本包标识无效，请检查 manifest.json 的 file_name')
            ok, message, output = export_script([p.name for p in stage.glob('*.py')], package_id,
                                               old.get('script_name', entry['name']), old.get('version', '1.0.0'),
                                               task_folder=str(stage), output_folder=temporary)
            if not ok:
                raise ValueError(message)
            # Replace is atomic even when the destination already exists.
            pending = destination.with_name('.' + destination.name + '.' + uuid.uuid4().hex + '.tmp')
            try:
                shutil.copyfile(output, pending)
                os.replace(pending, destination)
            finally:
                pending.unlink(missing_ok=True)

    def import_package(self, path):
        from ok.core.script_packager import import_script
        import zipfile
        with zipfile.ZipFile(path) as package:
            manifest = json.loads(package.read('manifest.json').decode('utf-8-sig'))
            package_id = str(manifest.get('file_name', '')).strip()
            self.resolve('ok_import/' + package_id)
            for info in package.infolist():
                if ':' in info.filename:
                    raise ValueError('脚本包包含无效的 Windows 文件名')
        ok, message, folder = import_script(str(path), import_base=str(self.data / 'ok_import'))
        if not ok:
            raise ValueError(message)
        # Importing never turns a task on. The user starts/enables it explicitly.
        state = self.settings()
        for entry in self.catalog():
            if self.resolve(entry['path']).parent == Path(folder).resolve():
                state.setdefault(entry['id'], {})['enabled'] = False
        self.save_settings(state)
        return folder
