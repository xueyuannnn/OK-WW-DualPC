"""Display localization must cover the palette and preserve executable metadata."""
import copy
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'native/bridge'), str(ROOT/'vendor/ok-script')]
from ui_text import ACTION_NAMES, PARAM_NAMES, localize_templates, workshop_text
from ok.core.script_templates import get_script_templates


class ChinesePresentationTests(unittest.TestCase):
    def test_settings_labels_and_help_are_chinese_without_changing_option_values(self):
        catalog = json.loads((ROOT/'native/resources/catalog.json').read_text(encoding='utf-8'))
        for group in catalog['globals'] + catalog['tasks']:
            for field in group.get('fields', []):
                self.assertRegex(field['label'], '[\u4e00-\u9fff]')
                if field.get('description'):
                    self.assertRegex(field['description'], '[\u4e00-\u9fff]')
                if field['key'] == 'Game Package':
                    self.assertEqual(field['options'], ['sd', 'hd', 'uhd'])
                    self.assertEqual(field['option_labels'], ['标准画质', '高清画质', '超高清画质'])

    def test_all_current_actions_and_parameters_have_chinese_labels(self):
        templates = get_script_templates()
        self.assertGreaterEqual(len(templates), 81)
        self.assertFalse({t['name'] for t in templates} - ACTION_NAMES.keys())
        self.assertFalse({p[0] for t in templates for p in t['params']} - PARAM_NAMES.keys())
        for item in localize_templates(templates):
            for text in (item['display_name'], item['category_label'], item['doc_zh'],
                         *item['param_labels'].values(), *item['param_docs_zh'].values()):
                self.assertRegex(text, '[\u4e00-\u9fff]')

    def test_presentation_does_not_rewrite_functions_defaults_or_docs(self):
        original = get_script_templates()
        before = copy.deepcopy(original)
        localized = localize_templates(original)
        self.assertEqual(original, before)
        for source, display in zip(original, localized):
            for key in source:
                self.assertEqual(display[key], source[key])

    def test_workshop_aliases_names_and_descriptions_preserve_source(self):
        characters = json.loads((ROOT/'native/resources/catalog.json').read_text(encoding='utf-8'))['characters']
        item = dict(team='Buling, Shorekeeper, Suoming', name='Buling_Shorekeeper_Suoming',
                    author='OriginalAuthor', description='Tips: boss buff ping。False https://example.com/boss')
        before = dict(item)
        result = workshop_text(item, characters)
        self.assertEqual(item, before)
        self.assertEqual(result['team_display'], '卜灵 / 守岸人 / 锁瞑')
        self.assertNotIn('Shorekeeper', result['name_display'])
        self.assertIn('提示', result['description_display'])
        self.assertIn('https://example.com/boss', result['description_display'])

    def test_missing_description_is_clear_without_inventing_requirements(self):
        result = workshop_text(dict(team='Baizhi, Verina, Buling', name='队伍方案', description='11'), [])
        self.assertEqual(result['description_display'], '作者暂未提供详细使用说明。原说明：11')
        adjacent = workshop_text(dict(team='', name='速刷boss', description='boss转火和buff效果'), [])
        self.assertEqual(adjacent['name_display'], '速刷首领')
        self.assertEqual(adjacent['description_display'], '首领转火和增益效果')


if __name__ == '__main__':
    unittest.main()
