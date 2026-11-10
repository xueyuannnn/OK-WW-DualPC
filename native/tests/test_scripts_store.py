from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
import json

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'native/bridge'), str(ROOT/'vendor/ok-script'), str(ROOT)]
from scripts_store import ScriptStore


class ScriptStoreTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.data = Path(self.temporary.name)
        self.store = ScriptStore(self.data)

    def tearDown(self):
        self.temporary.cleanup()

    def test_create_save_copy_delete_preserves_original(self):
        ident = self.store.create('Example')
        original = self.store.get(ident)['source']
        self.store.save(ident, original.replace('新脚本', '中文脚本'))
        clone = self.store.create('Copy', ident)
        self.assertEqual(self.store.get(clone)['name'], '中文脚本')
        self.store.remove(ident)
        self.assertEqual(len(self.store.catalog()), 1)
        self.assertEqual(len(list((self.data/'deleted-scripts').glob('*/*.py'))), 1)

    def test_syntax_error_does_not_replace_file_and_catalog_never_executes(self):
        ident = self.store.create('Example')
        original = self.store.get(ident)['source']
        with self.assertRaises(SyntaxError):
            self.store.save(ident, 'class Bad(:')
        self.assertEqual(self.store.get(ident)['source'], original)
        marker = self.data/'executed.txt'
        self.store.save(ident, f'from pathlib import Path\nPath({str(marker)!r}).touch()\n'+original)
        self.store.catalog()
        self.assertFalse(marker.exists())

    def test_settings_and_helper_modules(self):
        ident = self.store.create('Example')
        self.store.configure(ident, '_repeat', 5)
        self.store.configure(ident, '_continuous', True)
        (self.data/'ok_tasks/LabelEnum.py').write_text('from enum import Enum\nclass Labels(Enum):\n    A="x"\n')
        self.assertEqual(len(self.store.catalog()), 1)
        self.assertEqual(self.store.get(ident)['repeat'], 5)
        self.assertTrue(self.store.get(ident)['continuous'])
        with self.assertRaises(ValueError):
            self.store.configure(ident, '_repeat', 0)

    def test_original_package_roundtrip_with_resources(self):
        ident = self.store.create('Example')
        assets = self.data/'ok_tasks/assets'
        assets.mkdir()
        (assets/'data.txt').write_text('resource')
        archive = self.data/'export.okscript'
        self.store.export(ident, archive)
        manifest_before = (self.data/'ok_tasks/manifest.json').exists()
        self.assertFalse(manifest_before)
        self.store.import_package(archive)
        imported = next(s for s in self.store.catalog() if s['path'].startswith('ok_import/'))
        self.assertEqual(imported['source'], self.store.get(ident)['source'])
        self.assertFalse(imported['enabled'])
        self.assertEqual((self.data/'ok_import/custom_scripts/assets/data.txt').read_text(), 'resource')

    def test_archive_traversal_is_rejected_before_writes(self):
        archive = self.data/'bad.okscript'
        with zipfile.ZipFile(archive, 'w') as package:
            package.writestr('manifest.json', json.dumps(dict(file_name='safe')))
            package.writestr('../escaped.txt', 'bad')
        with self.assertRaises(ValueError):
            self.store.import_package(archive)
        self.assertFalse((self.data/'escaped.txt').exists())
        self.assertFalse((self.data/'ok_import/safe').exists())


if __name__ == '__main__':
    unittest.main()
