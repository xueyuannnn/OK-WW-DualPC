"""New upstream characters must work through the retained workshop importer."""
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['Aalto', 'Lingyang', 'Lumi', 'Yangyang', 'Suoming']


def test_python_registry_and_native_recognition_agree():
    from src.char.CharFactory import char_dict
    python_classes = {item['cls'].__name__ for item in char_dict.values()}
    definitions = json.loads((ROOT / 'native/resources/characters.json').read_text(encoding='utf-8'))
    native_classes = {item['class'] for item in definitions['characters']}
    assert python_classes == native_classes
    assert set(NAMES) <= native_classes
    assert len(native_classes) == 57


def test_new_characters_pass_original_zip_import_and_mode_switch(tmp_path, monkeypatch):
    from ok.util.config import Config
    from src.char import CustomCharLoader as loader
    monkeypatch.setattr(Config, 'config_folder', str(tmp_path / 'configs-dualpc'))
    for name in NAMES:
        members = sorted([name, 'Sanhua', 'Verina'], key=str.casefold)
        package = tmp_path / (name + '.zip')
        with zipfile.ZipFile(package, 'w') as archive:
            archive.writestr('team.json', json.dumps(dict(
                team=', '.join(members), name=name, author='upstream integration test',
                version='1.0', description='Importer compatibility fixture')))
            for member in members:
                archive.writestr(member + '.py',
                    f'from src.char.{member} import {member} as Builtin{member}\n'
                    f'class {member}(Builtin{member}):\n'
                    f'    def do_perform(self):\n        self.switch_next_char()\n')
        loader.import_custom_team(loader.inspect_team_archive(package))
        loader.switch_team_code_mode(members, 'import')
        assert loader.get_team_code_mode(members) == 'import'
        loader.switch_team_code_mode(members, 'builtin')
        assert loader.get_team_code_mode(members) == 'builtin'


def test_click_until_checks_once_without_input_at_zero_timeout():
    from src.char.Suoming import Suoming
    from types import SimpleNamespace
    from unittest.mock import Mock
    task = SimpleNamespace(char_config={})
    character = Suoming(task, 0)
    character.click = Mock()
    condition = Mock(return_value=False)
    assert character.continues_click_until(condition, 0) is False
    condition.assert_called_once_with()
    character.click.assert_not_called()


def test_click_until_uses_frame_wait_and_keeps_combat_checks():
    from src.char.Suoming import Suoming
    from types import SimpleNamespace
    from unittest.mock import Mock
    task = SimpleNamespace(char_config={}, wait_until=Mock(return_value=False))
    character = Suoming(task, 0)
    condition = Mock(return_value=False)
    character.click = Mock()
    assert character.continues_click_until(condition, 15) is False
    arguments = task.wait_until.call_args.kwargs
    assert arguments['time_out'] == 15
    assert arguments['pre_action'] == character.check_combat
    arguments['post_action']()
    character.click.assert_called_once_with(interval=.1)
