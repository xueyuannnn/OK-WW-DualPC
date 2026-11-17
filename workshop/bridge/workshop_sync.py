"""Offline-first mirror of the original character workshop; never runs code."""
from __future__ import annotations

import ast
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import io
import json
from pathlib import Path
import re
import time
from urllib.parse import urlparse
from urllib.request import Request, urlopen
import zipfile
import tempfile
import threading

from scripts_store import atomic_text

# Public rescue snapshot of the original workshop. The original distribution
# endpoint is currently unavailable; this mirror preserves the original authors.
DEFAULT_SOURCE = 'https://raw.githubusercontent.com/yamadafz/ok-ww-char-code/main/teams.json'
HOSTS = {'raw.githubusercontent.com', 'okwwcharcode.ok-script.com', 'cfokwwchar.ok-script.com'}


def source_url(value):
    value = str(value).strip()
    parsed = urlparse(value)
    if parsed.hostname == 'github.com' and parsed.scheme == 'https':
        parts = parsed.path.strip('/').split('/')
        if len(parts) == 2:
            value = f'https://raw.githubusercontent.com/{parts[0]}/{parts[1]}/main/teams.json'
        elif len(parts) >= 4 and parts[2] in {'blob', 'tree'}:
            tail = '/'.join(parts[4:]) or 'teams.json'
            value = f'https://raw.githubusercontent.com/{parts[0]}/{parts[1]}/{parts[3]}/{tail}'
    parsed = urlparse(value)
    if (parsed.scheme != 'https' or parsed.hostname not in HOSTS or parsed.username or parsed.password
            or parsed.port not in {None, 443} or parsed.fragment or parsed.query):
        raise ValueError('请填写原工坊 GitHub 仓库或 teams.json 的 HTTPS 地址')
    if not parsed.path.endswith('/teams.json'):
        raise ValueError('工坊源需提供原版 teams.json 索引')
    return value


def download(url, limit):
    parsed = urlparse(url)
    if parsed.scheme != 'https' or parsed.hostname not in HOSTS or parsed.username or parsed.password or parsed.port not in {None, 443}:
        raise ValueError('工坊下载地址不受支持')
    # Same distribution endpoint and client identifier as the original workshop.
    if parsed.hostname == 'okwwcharcode.ok-script.com':
        url = parsed._replace(netloc='cfokwwchar.ok-script.com').geturl()
    request = Request(url, headers={'User-Agent': 'ok-wuthering-waves'})
    with urlopen(request, timeout=10) as response:
        final = urlparse(response.url)
        if final.scheme != 'https' or final.hostname not in HOSTS:
            raise ValueError('工坊下载跳转到未知来源')
        content = response.read(limit + 1)
    if len(content) > limit:
        raise ValueError('工坊文件超过大小限制')
    return content


def archive_manifest(content):
    """Validate syntax and a flat 3-character ZIP without importing Python."""
    with zipfile.ZipFile(io.BytesIO(content)) as archive:
        files = archive.infolist()
        names = [item.filename for item in files]
        if len(files) != 4 or len(set(names)) != 4 or 'team.json' not in names:
            raise ValueError('队伍包需包含 team.json 和三个角色 Python 文件')
        if any('/' in n or '\\' in n or ':' in n or n in {'.', '..'} for n in names):
            raise ValueError('队伍包包含无效路径')
        if any(item.file_size > 2_000_000 or (item.external_attr >> 16) & 0o170000 == 0o120000 for item in files):
            raise ValueError('队伍包包含过大的文件或链接')
        manifest = json.loads(archive.read('team.json').decode('utf-8-sig'))
        for key in ('team', 'name', 'author', 'version', 'description'):
            if not isinstance(manifest.get(key), str) or not manifest[key].strip():
                raise ValueError('队伍包缺少 ' + key)
        members = [n.strip() for n in manifest['team'].split(',')]
        if len(members) != 3 or len(set(members)) != 3:
            raise ValueError('队伍包需要三个不同角色')
        if any(not n.endswith('.py') for n in names if n != 'team.json'):
            raise ValueError('队伍包包含非角色文件')
        for name in names:
            if name.endswith('.py'):
                ast.parse(archive.read(name).decode('utf-8-sig'), filename=name)
        return manifest


def entries(payload):
    if not isinstance(payload, dict) or not isinstance(payload.get('teams'), list):
        raise ValueError('原工坊 teams.json 格式无效')
    result = {}
    for team in payload['teams']:
        if not isinstance(team, dict):
            continue
        for code in team.get('codes', []):
            if not isinstance(code, dict):
                continue
            item = {key: code.get(key, '') for key in ('name', 'description', 'author', 'version', 'filename', 'timestamp')}
            item['team'] = code.get('team') or team.get('team', '')
            item['url'] = code.get('rawUrl') or code.get('downloadUrl') or ''
            parsed = urlparse(item['url'])
            if parsed.scheme != 'https' or parsed.hostname not in HOSTS:
                continue
            item['id'] = hashlib.sha256(json.dumps(item, ensure_ascii=False, sort_keys=True).encode()).hexdigest()
            result[item['id']] = item
    if len(result) > 2000:
        raise ValueError('工坊索引条目过多')
    return list(result.values())


class WorkshopLibrary:
    def __init__(self, data, fetch=download, supported=None, characters=None):
        self.root = Path(data) / 'workshop-library'
        self.fetch = fetch
        self.supported = {name.casefold() for name in supported} if supported is not None else None
        self.characters = characters or []
        self.storage_lock = threading.Lock()
        self.deadline = float('inf')
        self.stored_bytes = 0

    def read(self, name, fallback):
        path = self.root / name
        return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else fallback

    def write(self, name, value):
        atomic_text(self.root / name, json.dumps(value, ensure_ascii=False, indent=2) + '\n')

    def seed_local(self):
        """Keep already imported variants available even when upstream is down."""
        profile = self.root.parent / 'configs-dualpc/custom_teams'
        index = self.read('index.json', {})
        values = index.get('entries', [])
        known = {item['id'] for item in values}
        added = 0
        for folder in sorted(profile.glob('*')):
            scripts = sorted(folder.glob('*.import.py')) if folder.is_dir() else []
            if len(scripts) != 3 or not (folder/'team.json').is_file():
                continue
            try:
                manifest = json.loads((folder/'team.json').read_text(encoding='utf-8-sig'))
                manifest.setdefault('name', folder.name)
                manifest.setdefault('description', '本地保存的工坊队伍')
                stream = io.BytesIO()
                with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED) as archive:
                    archive.writestr(zipfile.ZipInfo('team.json'), json.dumps(manifest, ensure_ascii=False))
                    for script in scripts:
                        archive.writestr(zipfile.ZipInfo(script.name.replace('.import.py', '.py')), script.read_bytes())
                content = stream.getvalue()
                archive_manifest(content)
                sha = hashlib.sha256(content).hexdigest()
                ident = hashlib.sha256(('local:' + sha).encode()).hexdigest()
                if ident in known:
                    continue
                path = self.archive_path(sha)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
                values.append(dict(manifest, id=ident, sha256=sha, manifest=manifest, url='', local=True))
                known.add(ident)
                added += 1
            except (ValueError, OSError, SyntaxError, zipfile.BadZipFile):
                continue  # The active profile is authoritative and never modified.
        if added:
            index['entries'] = values
            self.write('index.json', index)
        return added

    def settings(self):
        return dict(source=DEFAULT_SOURCE, auto_update=True, interval_hours=24) | self.read('settings.json', {})

    def configure(self, source, auto_update, interval_hours=24):
        if isinstance(interval_hours, bool) or not isinstance(interval_hours, int) or not 1 <= interval_hours <= 720:
            raise ValueError('工坊更新周期应为 1 至 720 小时')
        new_source = source_url(source)
        if new_source != self.settings()['source']:
            index = self.read('index.json', {})
            index['attempted_at'] = 0
            self.write('index.json', index)
        self.write('settings.json', dict(source=new_source, auto_update=bool(auto_update), interval_hours=interval_hours))

    def archive_path(self, sha):
        if not re.fullmatch('[0-9a-f]{64}', sha):
            raise ValueError('工坊缓存标识无效')
        return self.root / 'archives' / (sha + '.zip')

    def cached(self, item):
        sha = item.get('sha256', '')
        if not re.fullmatch('[0-9a-f]{64}', sha):
            return False
        path = self.archive_path(sha)
        return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == sha

    def catalog(self):
        index = self.read('index.json', {})
        values = index.get('entries', [])
        for item in values:
            item['downloaded'] = self.cached(item)
            from ui_text import workshop_text
            item.update(workshop_text(item, self.characters))
            if self.supported is not None:
                # Original workshop display names differ from the native class
                # names for these characters (CustomCharLoader display registry).
                aliases = {'lynae': 'linnai', 'buling': 'douling', 'sigrika': 'xigelika',
                           'yangyang: xuanling': 'yangyangsp', 'luuk herssen': 'luhesi',
                           'xiangli yao': 'xiangliyao', 'rover': 'havocrover'}
                missing = [name.strip() for name in str(item.get('team', '')).split(',')
                           if name.strip().casefold() not in self.supported
                           and aliases.get(name.strip().casefold(), name.strip().casefold()) not in self.supported]
                item['unsupported_members'] = missing
                item['compatible'] = not missing
                if missing:
                    item['source_description'] = item.get('description', '')
                    item['description'] = '【待适配角色：' + '、'.join(missing) + '】' + item['source_description']
                    item['description_display'] = '【角色待适配】' + item['description_display']
        return dict(self.settings(), entries=values, last_checked=index.get('last_checked', ''),
                    error=index.get('error', ''), downloaded=sum(item['downloaded'] for item in values),
                    total=len(values))

    def refresh(self, due=False):
        settings = self.settings()
        previous = self.read('index.json', {})
        if due and (not settings['auto_update'] or time.time() - previous.get('attempted_at', 0) < settings['interval_hours'] * 3600):
            return self.catalog()
        index = dict(previous, attempted_at=time.time())
        try:
            values = entries(json.loads(self.fetch(source_url(settings['source']), 8_000_000)))
            values += [item for item in previous.get('entries', []) if item.get('local')]
            old = {item['id']: item for item in previous.get('entries', [])}
            for item in values:
                if item['id'] in old:
                    item.update({k: old[item['id']][k] for k in ('sha256', 'manifest') if k in old[item['id']]})
            index.update(entries=values, error='', last_checked=time.strftime('%Y-%m-%d %H:%M:%S'))
            self.write('index.json', index)
            self.download_all()
        except Exception as error:
            index['error'] = '同步未完成，已保留本地代码库：' + str(error)
            self.write('index.json', index)
        return self.catalog()

    def fetch_item(self, item):
        if self.cached(item):
            return item
        if time.monotonic() > self.deadline:
            raise ValueError('本次同步达到两分钟限制，请点击下载全部继续')
        content = self.fetch(item['url'], 20_000_000)
        manifest = archive_manifest(content)
        expected = sorted(str(item.get('team', '')).replace(' ', '').split(','))
        if expected != sorted(manifest['team'].replace(' ', '').split(',')):
            raise ValueError('队伍包与工坊索引中的角色不一致')
        sha = hashlib.sha256(content).hexdigest()
        path = self.archive_path(sha)
        path.parent.mkdir(parents=True, exist_ok=True)
        # Publish only complete verified archives, never partial downloads.
        with self.storage_lock:
            if self.stored_bytes + len(content) > 500_000_000:
                raise ValueError('代码库达到 500 MB 容量限制')
            handle, temporary = tempfile.mkstemp(dir=path.parent, suffix='.tmp')
            import os
            try:
                with os.fdopen(handle, 'wb') as stream:
                    stream.write(content)
                Path(temporary).replace(path)
                self.stored_bytes += len(content)
            finally:
                Path(temporary).unlink(missing_ok=True)
        return dict(item, sha256=sha, manifest=manifest, error='')

    def download_all(self):
        index = self.read('index.json', {})
        values = index.get('entries', [])
        pending = [(i, item) for i, item in enumerate(values) if not self.cached(item)]
        self.stored_bytes = sum(path.stat().st_size for path in (self.root/'archives').glob('*.zip'))
        self.deadline = time.monotonic() + 120
        if self.stored_bytes > 500_000_000:
            raise ValueError('本地代码库超过 500 MB，请先检查工坊源')
        with ThreadPoolExecutor(max_workers=4) as pool:
            jobs = {pool.submit(self.fetch_item, item): i for i, item in pending}
            for job in as_completed(jobs):
                i = jobs[job]
                try:
                    values[i] = job.result()
                except Exception as error:
                    values[i]['error'] = str(error)
        index['entries'] = values
        failed = sum(bool(item.get('error')) for item in values)
        index['error'] = f'{failed} 个队伍包下载失败，可稍后重试；其余内容已保存' if failed else ''
        self.write('index.json', index)
        return self.catalog()

    def select(self, ident):
        index = self.read('index.json', {})
        for i, item in enumerate(index.get('entries', [])):
            if item['id'] == ident:
                item = self.fetch_item(item)
                index['entries'][i] = item
                self.write('index.json', index)
                from ui_text import workshop_text
                return dict(path=str(self.archive_path(item['sha256']).resolve()), manifest=item['manifest'],
                            **workshop_text(item, self.characters))
        raise ValueError('该工坊版本已不存在，请刷新列表')


def command(data, q, root=None):
    supported = None
    characters = []
    if root is not None:
        root = Path(root)
        for file in (root/'native/resources/catalog.json', root.parent/'native/resources/catalog.json'):
            if file.is_file():
                characters = json.loads(file.read_text(encoding='utf-8'))['characters']
                supported = [c['id'] for c in characters]
                break
    library = WorkshopLibrary(data, supported=supported, characters=characters)
    op = q['op']
    if op != 'library':
        library.seed_local()
    if op == 'library_settings':
        library.configure(q['source'], q['auto_update'], q.get('interval_hours', 24))
    elif op == 'library_refresh':
        return library.refresh(q.get('due', False))
    elif op == 'library_download':
        return library.download_all()
    elif op == 'library_select':
        return library.select(q['entry'])
    elif op != 'library':
        raise ValueError('未知工坊同步操作')
    return library.catalog()
