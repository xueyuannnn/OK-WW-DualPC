import json
from pathlib import Path
from types import SimpleNamespace

from dualpc.bootstrap import make_config
from dualpc.settings import Settings
from tools_dualpc.package_personal import settings_snapshot


def test_dualpc_has_no_game_launcher_settings_and_login_keeps_original_config():
    from src.task.AutoLoginTask import AutoLoginTask
    config = make_config(Settings(), Path.cwd())
    assert all(option.name != 'Game Startup' for option in config['global_configs'])
    assert all('Launch Game After Capture Starts' not in option.default_config for option in config['global_configs'])
    executor = SimpleNamespace(scene=None, text_fix={}, global_config=SimpleNamespace(get_config=lambda name: {}))
    task = AutoLoginTask(executor, None)
    assert task.default_config == {'_enabled': True}
    assert not task.config_type


def test_personal_snapshot_preserves_user_settings_and_omits_retired_launcher_config(tmp_path):
    folder = tmp_path/'configs-dualpc'
    folder.mkdir()
    (tmp_path/'dualpc.json').write_text('{}')
    login = folder/'AutoLoginTask.json'
    login.write_text(json.dumps({'_enabled': False, 'Auto Launch Game': True,
                                'Game PC Launch Path': 'D:\\Games\\launcher.exe'}))
    before = login.read_bytes()
    (folder/'Game Startup.json').write_text('{}')
    (folder/'GameStartupTask.json').write_text('{}')
    preview = folder/'preview.json'
    preview.write_text('{"fps": 144}')
    snapshot = settings_snapshot(tmp_path)
    assert json.loads(snapshot['configs-dualpc/AutoLoginTask.json']) == {'_enabled': False}
    assert snapshot['configs-dualpc/preview.json'] == preview.read_bytes()
    assert set(snapshot) == {'dualpc.json', 'configs-dualpc/AutoLoginTask.json', 'configs-dualpc/preview.json'}
    assert login.read_bytes() == before
