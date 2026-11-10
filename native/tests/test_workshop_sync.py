import io
import json
from pathlib import Path
import sys
import zipfile

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'bridge'))
from workshop_sync import DEFAULT_SOURCE, WorkshopLibrary, archive_manifest, entries, source_url


def archive(version='1.0', invalid=False):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w') as value:
        value.writestr('team.json', json.dumps(dict(name='Test', description='Test rotation',
                        team='Baizhi, Mortefi, Verina', author='Author', version=version)))
        for member in ('Baizhi', 'Mortefi', 'Verina'):
            value.writestr(member + '.py', 'raise RuntimeError("must not execute")\n' if not invalid else 'class :')
    return stream.getvalue()


def index():
    return dict(teams=[dict(team='Baizhi, Mortefi, Verina', codes=[dict(name='Test', author='Author',
                        version='1.0', rawUrl='https://cfokwwchar.ok-script.com/codes/test.zip')])])


def test_full_refresh_is_offline_ready_without_changing_active_teams(tmp_path):
    active = tmp_path/'configs-dualpc/custom_teams/current.py'
    active.parent.mkdir(parents=True)
    active.write_text('personal code', encoding='utf-8')
    calls = []
    def fetch(url, limit):
        calls.append(url)
        return json.dumps(index()).encode() if url.endswith('teams.json') else archive()
    library = WorkshopLibrary(tmp_path, fetch)
    library.configure('https://cfokwwchar.ok-script.com/teams.json', True)
    result = library.refresh()
    assert result['total'] == result['downloaded'] == 1
    selection = library.select(result['entries'][0]['id'])
    assert Path(selection['path']).is_file()
    assert selection['manifest']['author'] == 'Author'
    assert active.read_text() == 'personal code'
    assert len(calls) == 2
    library.refresh(due=True)
    assert len(calls) == 2


def test_failed_update_preserves_local_library_and_can_retry(tmp_path):
    library = WorkshopLibrary(tmp_path, lambda url, limit: json.dumps(index()).encode() if url.endswith('teams.json') else archive())
    original = library.refresh()
    library.fetch = lambda *_: (_ for _ in ()).throw(OSError('network unavailable'))
    failed = library.refresh()
    assert failed['entries'][0]['sha256'] == original['entries'][0]['sha256']
    assert failed['downloaded'] == 1
    assert 'network unavailable' in failed['error']
    cached = Path(library.select(failed['entries'][0]['id'])['path'])
    cached.write_bytes(b'corrupt')
    with pytest.raises(OSError):
        library.select(failed['entries'][0]['id'])


def test_sources_and_archives_are_validated_without_executing(tmp_path):
    assert source_url('https://github.com/example/workshop') == 'https://raw.githubusercontent.com/example/workshop/main/teams.json'
    for value in ('http://cfokwwchar.ok-script.com/teams.json', 'https://localhost/teams.json',
                  'https://raw.githubusercontent.com:123/a/b/main/teams.json'):
        with pytest.raises(ValueError):
            source_url(value)
    assert archive_manifest(archive())['version'] == '1.0'
    with pytest.raises(SyntaxError):
        archive_manifest(archive(invalid=True))
    broken = io.BytesIO()
    with zipfile.ZipFile(broken, 'w') as value:
        value.writestr('../outside.py', 'pass')
    with pytest.raises(ValueError):
        archive_manifest(broken.getvalue())


def test_version_variants_remain_distinct(tmp_path):
    payload = index()
    second = dict(payload['teams'][0]['codes'][0], version='2.0', rawUrl='https://cfokwwchar.ok-script.com/codes/test2.zip')
    payload['teams'][0]['codes'].append(second)
    assert len(entries(payload)) == 2
    assert entries(payload)[0]['id'] != entries(payload)[1]['id']


def test_default_source_is_the_original_workshop_rescue_snapshot(tmp_path):
    library = WorkshopLibrary(tmp_path)
    assert library.settings()['source'] == DEFAULT_SOURCE
    assert source_url(DEFAULT_SOURCE) == 'https://raw.githubusercontent.com/yamadafz/ok-ww-char-code/main/teams.json'
    assert library.settings()['auto_update']


def test_unknown_characters_are_labeled_without_hiding_or_modifying_original_metadata(tmp_path):
    library = WorkshopLibrary(tmp_path, supported={'Hsin', 'ShoreKeeper', 'Linnai', 'Douling',
                                                  'Xigelika', 'YangYangSp', 'Luhesi', 'Xiangliyao', 'HavocRover'})
    library.write('index.json', dict(entries=[
        dict(id='unsupported', team='Hsin, Shorekeeper, Suoming', description='original description'),
        dict(id='alias', team='Hsin, Shorekeeper, Lynae', description='known aliases'),
        dict(id='more_aliases', team='Buling, Sigrika, Yangyang: Xuanling', description='display names'),
        dict(id='more_names', team='Luuk Herssen, Xiangli Yao, Rover', description='display names')]))
    rows = library.catalog()['entries']
    assert len(rows) == 4
    assert rows[0]['unsupported_members'] == ['Suoming']
    assert rows[0]['source_description'] == 'original description'
    assert rows[0]['description'].startswith('【待适配角色：Suoming】')
    assert rows[1]['compatible']
    assert rows[2]['compatible']
    assert rows[3]['compatible']
    assert library.read('index.json', {})['entries'][0]['description'] == 'original description'


def test_update_cycle_is_saved_and_due_check_respects_it(tmp_path, monkeypatch):
    clock = [1800000000.]
    monkeypatch.setattr('workshop_sync.time.time', lambda: clock[0])
    calls = []
    def fetch(url, limit):
        calls.append(url)
        return json.dumps(index()).encode() if url.endswith('teams.json') else archive()
    library = WorkshopLibrary(tmp_path, fetch)
    library.configure('https://cfokwwchar.ok-script.com/teams.json', True, 6)
    result = library.refresh(due=True)
    assert result['interval_hours'] == 6
    assert result['downloaded'] == 1
    assert len(calls) == 2
    clock[0] += 5 * 3600
    library.refresh(due=True)
    assert len(calls) == 2
    clock[0] += 3601
    library.refresh(due=True)
    assert len(calls) == 3
    library.configure('https://cfokwwchar.ok-script.com/teams.json', False, 1)
    clock[0] += 7200
    library.refresh(due=True)
    assert len(calls) == 3
    for invalid in (0, 721, 1.5, True):
        with pytest.raises(ValueError):
            library.configure('https://cfokwwchar.ok-script.com/teams.json', True, invalid)


def test_local_imports_are_mirrored_without_executing_or_losing_them_on_refresh(tmp_path):
    folder = tmp_path/'configs-dualpc/custom_teams/Baizhi__Mortefi__Verina'
    folder.mkdir(parents=True)
    with zipfile.ZipFile(io.BytesIO(archive())) as value:
        for name in value.namelist():
            (folder/(name.replace('.py', '.import.py'))).write_bytes(value.read(name))
    library = WorkshopLibrary(tmp_path, lambda url, limit: b'{"teams": []}')
    assert library.seed_local() == 1
    assert library.seed_local() == 0
    result = library.refresh()
    assert result['total'] == result['downloaded'] == 1
    assert result['entries'][0]['local']
    assert (folder/'Baizhi.import.py').is_file()
