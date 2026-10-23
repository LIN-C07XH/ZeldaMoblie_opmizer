"""Exercise source installation without a Git checkout or game assets."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'script'))
import overlay_package as package


class OverlayPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.target, self.source = self.root / 'target', self.root / 'overlay'
        for path in (self.target, self.source):
            for name in ('host', 'tools', 'script'):
                (path / name).mkdir(parents=True)
        (self.target / 'tools/game_timing.py').write_text('# baseline\n')
        (self.target / 'host/input.c').write_text('original\n')
        (self.source / 'host/input.c').write_text('patched\n')
        (self.source / 'script/new.sh').write_text('#!/bin/sh\n')
        (self.source / 'script/new.sh').chmod(0o755)
        self.manifest = {'version': 1, 'upstream': {'commit': 'fixture'},
            'required': {'tools/game_timing.py': package.fingerprint(self.target / 'tools/game_timing.py')},
            'files': {name: {'base': package.fingerprint(self.target / name),
                            'overlay': package.fingerprint(self.source / name)}
                      for name in ('host/input.c', 'script/new.sh')}}
        self.original = self.snapshot()
        self.root_patch = patch.object(package, 'ROOT', self.source)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        self.addCleanup(self.temp.cleanup)

    def snapshot(self):
        return {str(p.relative_to(self.target)): package.fingerprint(p)
                for p in self.target.rglob('*') if p.is_file() and '.camera_backup' not in p.parts}

    def apply(self, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            package.apply(self.target, self.manifest, **kwargs)

    def test_roundtrip_reinstall_modes_and_private_data(self):
        (self.target / 'work').mkdir()
        (self.target / 'work/settings.json').write_text('user settings')
        original = self.snapshot()
        self.apply(check=True)
        self.assertFalse((self.target / '.camera_backup').exists())
        self.apply(); self.apply()
        self.assertEqual(package.fingerprint(self.target / 'script/new.sh')['mode'], 0o755)
        self.apply(uninstall=True); self.apply(uninstall=True)
        self.assertEqual(self.snapshot(), original)
        self.apply(); self.apply(uninstall=True)
        self.assertEqual(self.snapshot(), original)

    def test_preflight_preserves_all_files_on_local_edit_or_wrong_base(self):
        (self.target / 'host/input.c').write_text('user edit')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Local changes'):
            self.apply()
        self.assertEqual(self.snapshot(), before)
        self.assertFalse((self.target / '.camera_backup').exists())
        (self.target / 'host/input.c').write_text('original\n')
        (self.target / 'tools/game_timing.py').write_text('different upstream')
        with self.assertRaisesRegex(ValueError, 'Unsupported upstream'):
            self.apply()
        self.assertFalse((self.target / '.camera_backup').exists())

    def test_legacy_backups_are_not_replaced(self):
        backup = self.target / '.camera_backup'
        (backup / 'host').mkdir(parents=True)
        (backup / 'host/input.c').write_text('pre-overlay custom version\n')
        (backup / 'manifest.txt').write_text('host/input.c\n')
        (self.target / 'host/input.c').write_text('patched\n')
        self.apply(); self.apply()
        self.apply(uninstall=True)
        self.assertEqual((self.target / 'host/input.c').read_text(), 'pre-overlay custom version\n')
        self.assertFalse((self.target / 'script/new.sh').exists())

    def test_modified_install_or_backup_blocks_uninstall(self):
        self.apply()
        (self.target / 'script/new.sh').write_text('local new changes')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Local changes'):
            self.apply(uninstall=True)
        self.assertEqual(self.snapshot(), before)
        (self.target / 'script/new.sh').write_text('#!/bin/sh\n')
        (self.target / '.camera_backup/host/input.c').write_text('lost original')
        with self.assertRaisesRegex(ValueError, 'Backup was changed'):
            self.apply(uninstall=True)

    def test_payload_write_failure_rolls_back_and_can_retry(self):
        original_write = package.replace_bytes
        failed = False
        def fail_once(path, data, mode=0o644):
            nonlocal failed
            if path == self.target / 'script/new.sh' and not failed:
                failed = True
                raise OSError('simulated disk failure')
            original_write(path, data, mode)
        with patch.object(package, 'replace_bytes', fail_once):
            with self.assertRaisesRegex(OSError, 'simulated disk failure'):
                self.apply()
        self.assertEqual(self.snapshot(), self.original)
        self.apply(); self.apply(uninstall=True)
        self.assertEqual(self.snapshot(), self.original)

    def test_symbolic_paths_and_traversal_are_rejected(self):
        (self.target / 'host/input.c').unlink()
        outside = self.root / 'outside.c'; outside.write_text('outside')
        (self.target / 'host/input.c').symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'symbolic link'):
            self.apply()
        self.assertEqual(outside.read_text(), 'outside')
        for name in ('../outside.c', '/host/input.c', 'host/../outside.c'):
            with self.assertRaises(ValueError):
                package.safe_path(self.target, name)

    def test_release_manifest_detects_missing_or_changed_payload(self):
        (self.source / 'overlay-manifest.json').write_text(json.dumps(self.manifest))
        self.assertEqual(package.load_manifest(), self.manifest)
        (self.source / 'host/input.c').write_text('unrecorded edit')
        with self.assertRaisesRegex(ValueError, 'differs from its manifest'):
            package.load_manifest()


if __name__ == '__main__':
    unittest.main()
