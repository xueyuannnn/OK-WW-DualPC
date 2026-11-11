"""Cross-language registration contracts; no app startup or hardware access."""
import ast
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_python_character_metadata_and_native_registration_stay_in_sync():
    source = ROOT / 'src/char/CharFactory.py'
    tree = ast.parse(source.read_text(encoding='utf-8-sig'))
    raw = next(n.value for n in tree.body if isinstance(n, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == '_char_dict_raw' for t in n.targets))
    expected = []
    for key, values in zip(raw.keys, raw.values):
        templates = [v.attr for v in key.elts] if isinstance(key, ast.Tuple) else [key.attr]
        entry = dict(templates=templates, canonical=templates[0])
        for field, value in zip(values.keys, values.values):
            field = ast.literal_eval(field)
            if field == 'cls':
                entry['class'] = value.id
            elif field == 'char_type':
                entry['role'] = {'MAIN_DPS': 0, 'SUB_DPS': 1, 'HEALER': 2}[value.attr]
            elif field == 'ring_index':
                entry['element'] = dict(SPECTRO=0, ELECTRIC=1, FIRE=2, ICE=3, WIND=4, HAVOC=5)[value.attr]
            else:
                entry[field] = ast.literal_eval(value)
        entry.setdefault('role', 0)
        entry.setdefault('element', -1)
        entry.setdefault('buff_time', 28 if entry['role'] == 2 else 14)
        expected.append(entry)
    actual = json.loads((ROOT / 'native/resources/characters.json').read_text(encoding='utf-8'))
    assert actual['characters'] == expected, 'Regenerate native character metadata after changing CharFactory'
    registered = set()
    for path in (ROOT / 'native/src/runtime').glob('characters_*.cpp'):
        registered.update(re.findall(r'\{\s*"(\w+)"\s*,\s*&?createCharacter<', path.read_text(encoding='utf-8')))
    registered.discard('BaseChar')
    assert {entry['class'] for entry in expected} == registered


def test_every_catalog_task_has_a_native_dispatch_entry():
    catalog = json.loads((ROOT / 'native/resources/catalog.json').read_text(encoding='utf-8'))
    runtime = ROOT / 'native/src/runtime'
    business = (runtime / 'task_registry.cpp').read_text(encoding='utf-8').split('bool TaskRunner::dispatch')[0]
    triggers = (runtime / 'task.cpp').read_text(encoding='utf-8').split('QVector<TaskDefinition> builtinTasks()')[1]
    registered = set(re.findall(r'"([A-Za-z]+Task2?)"', business + triggers))
    assert {entry['id'] for entry in catalog['tasks']} == registered
