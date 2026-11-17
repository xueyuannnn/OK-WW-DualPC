"""Exercise the shipped installer against isolated installations; never launch the app."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import shutil
import unittest
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / 'resources/install-update.ps1'


def sha(data):
    return hashlib.sha256(data).hexdigest()


@unittest.skipUnless(sys.platform == 'win32', 'Windows installer')
class AppUpdateTests(unittest.TestCase):
    def test_windows_powershell_encoding_is_explicit(self):
        # Windows PowerShell 5.1 otherwise decodes non-ASCII UTF-8 as the
        # machine ANSI code page before parsing, including quoted UI text.
        source = SCRIPT.read_bytes()
        self.assertTrue(source.startswith(b'\xef\xbb\xbf'))
        self.assertIn('软件', source.decode('utf-8-sig'))

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='okww-update-')
        def cleanup():
            target = Path(self.temp.name).resolve()
            assert target.parent == Path(tempfile.gettempdir()).resolve() and target.name.startswith('okww-update-')
            self.temp._finalizer.detach()
            shutil.rmtree('\\\\?\\' + str(target))
        self.addCleanup(cleanup)
        self.root = Path(self.temp.name) / '中文 personal install'
        self.root.mkdir()
        self.job = self.root / '.updates/job-test'
        self.job.mkdir(parents=True)
        self.plan = self.job / 'plan.json'
        self.exe = self.root / 'OKWW-DualPC-Native-Dev.exe'
        self.exe.write_bytes(b'old app')
        self.profile = self.root / 'native-full-data/configs-dualpc/teams.json'
        self.profile.parent.mkdir(parents=True)
        self.profile.write_bytes(b'private config')
        self.old_files = {'OKWW-DualPC-Native-Dev.exe': b'old app', 'obsolete.py': b'old managed code'}
        (self.root / 'obsolete.py').write_bytes(self.old_files['obsolete.py'])
        self.write_json(self.root / 'PACKAGE.json', {
            'variant': 'personal', 'files': [self.entry(k, v) for k, v in self.old_files.items()] +
            [self.entry('native-full-data/configs-dualpc/teams.json', b'private config')],
        })

    @staticmethod
    def write_json(path, value):
        path.write_text(json.dumps(value), encoding='utf-8')

    @staticmethod
    def entry(name, data):
        return {'path': name, 'bytes': len(data), 'sha256': sha(data)}

    def archive(self, extra=None, raw=None):
        files = {'OKWW-DualPC-Native-Dev.exe': b'new app',
                 'workshop-engine/runtime/python.exe': b'portable runtime',
                 'source/README.md': b'new source'}
        files.update(extra or {})
        manifest = {'variant': 'full', 'runtime': 'C++ + Python workshop', 'core_language': 'Rust',
                    'frontend': 'Qt6 Widgets', 'release_version': '0.2.0-beta.8',
                    'exe_sha256': sha(b'new app'), 'files': [self.entry(k, v) for k, v in files.items()]}
        with zipfile.ZipFile(self.job / 'package.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            for key, data in files.items():
                archive.writestr('OKWW-DualPC-full/' + key, data)
            archive.writestr('OKWW-DualPC-full/PACKAGE.json', json.dumps(manifest))
            for key, data in raw or []:
                archive.writestr(key, data)
        self.write_json(self.plan, {'schema': 1, 'root': str(self.root), 'data': str(self.profile.parents[1]),
                                   'job': str(self.job), 'pid': 2147483647, 'parent_start_ticks': '0',
                                   'sha256': sha((self.job / 'package.zip').read_bytes()), 'release': 'v0.2.0-beta.8'})

    def run_installer(self, mode, success=True, expected=None):
        process = subprocess.run([
            os.path.join(os.environ['SystemRoot'], 'System32/WindowsPowerShell/v1.0/powershell.exe'),
            '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(SCRIPT),
            '-Plan', str(self.plan), '-Mode', mode, '-NoRestart',
        ], capture_output=True, encoding='utf-8', errors='replace', timeout=90,
            creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(process.returncode == 0, success, process.stdout + process.stderr)
        if expected:
            self.assertIn(expected, process.stdout + process.stderr)
        return process

    def assert_original(self):
        self.assertEqual(self.exe.read_bytes(), b'old app')
        self.assertEqual((self.root / 'obsolete.py').read_bytes(), b'old managed code')
        self.assertEqual(self.profile.read_bytes(), b'private config')

    def test_short_windows_paths_use_one_normalization_rule(self):
        import ctypes
        from ctypes import wintypes
        short_path = ctypes.WinDLL('kernel32', use_last_error=True).GetShortPathNameW
        short_path.argtypes = (wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD)
        short_path.restype = wintypes.DWORD
        buffer = ctypes.create_unicode_buffer(32768)
        length = short_path(self.temp.name, buffer, len(buffer))
        self.assertGreater(length, 0)
        self.assertLess(length, len(buffer))
        if buffer.value.casefold() == self.temp.name.casefold():
            self.skipTest('8.3 filenames are disabled on this volume')
        self.root = Path(buffer.value) / self.root.relative_to(self.temp.name)
        self.job = self.root / '.updates/job-test'
        self.plan = self.job / 'plan.json'
        self.exe = self.root / 'OKWW-DualPC-Native-Dev.exe'
        self.profile = self.root / 'native-full-data/configs-dualpc/teams.json'
        self.archive()
        self.run_installer('Prepare')
        self.run_installer('Install')
        self.assertEqual(self.exe.read_bytes(), b'new app')
        self.assertEqual(self.profile.read_bytes(), b'private config')

    def test_complete_update_preserves_profile_and_removes_only_managed_files(self):
        local = self.root / 'my-custom-script.py'
        local.write_bytes(b'user script')
        self.archive()
        self.run_installer('Prepare')
        self.assert_original()
        self.run_installer('Install')
        self.assertEqual(self.exe.read_bytes(), b'new app')
        self.assertEqual(local.read_bytes(), b'user script')
        self.assertEqual(self.profile.read_bytes(), b'private config')
        self.assertFalse((self.root / 'obsolete.py').exists())
        manifest = json.loads((self.root / 'PACKAGE.json').read_text(encoding='utf-8-sig'))
        self.assertEqual(manifest['variant'], 'personal')
        self.assertEqual(manifest['release_version'], 'v0.2.0-beta.8')
        self.assertTrue(json.loads((self.job / 'result.json').read_text())['ok'])
        self.assertFalse((self.job / 'package.zip').exists())

    def test_bad_checksum_never_changes_installed_files(self):
        self.archive()
        with (self.job / 'package.zip').open('ab') as file:
            file.write(b'changed')
        self.run_installer('Prepare', False, 'Archive checksum mismatch')
        self.assert_original()

    def test_path_traversal_is_rejected(self):
        self.archive(raw=[('OKWW-DualPC-full/../../escaped.txt', b'invalid')])
        self.run_installer('Prepare', False, 'Unsafe package path')
        self.assert_original()
        self.assertFalse((self.root / 'escaped.txt').exists())

    def test_duplicate_windows_paths_are_rejected(self):
        self.archive(raw=[('OKWW-DualPC-full/SOURCE/readme.md', b'collision')])
        self.run_installer('Prepare', False, 'Forbidden archive entry')
        self.assert_original()

    def test_archive_cannot_replace_personal_data(self):
        self.archive(extra={'native-full-data/configs-dualpc/teams.json': b'overwrite'})
        self.run_installer('Prepare', False, 'Forbidden archive entry')
        self.assert_original()

    def test_modified_stage_is_rejected_again_before_install(self):
        self.archive()
        self.run_installer('Prepare')
        (self.job / 'stage/OKWW-DualPC-Native-Dev.exe').write_bytes(b'tampered')
        self.run_installer('Install', False, 'Package file verification failed')
        self.assert_original()

    def test_install_failure_rolls_back_files_already_replaced(self):
        self.archive(extra={'blocked/file.txt': b'new file'})
        (self.root / 'blocked').write_bytes(b'user file blocks a directory')
        self.run_installer('Prepare')
        self.run_installer('Install', False)
        self.assertIn('Installing update', (self.job / 'update.log').read_text(encoding='utf-8-sig'))
        self.assert_original()
        self.assertFalse((self.root / 'source/README.md').exists())
        self.assertEqual((self.root / 'blocked').read_bytes(), b'user file blocks a directory')

    def test_long_nested_package_paths(self):
        relative = 'source/' + '/'.join(['long-file-directory-' + 'x' * 45] * 4) + '/中文.txt'
        self.archive(extra={relative: b'long path content'})
        self.run_installer('Prepare')
        self.run_installer('Install')
        extended = '\\\\?\\' + str(self.root / relative)
        self.assertEqual(Path(extended).read_bytes(), b'long path content')


if __name__ == '__main__':
    unittest.main()
