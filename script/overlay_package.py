#!/usr/bin/env python3
"""Install a fingerprinted source overlay without Git or game resources."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = {'host', 'ios', 'script', 'tools', 'tests', 'docs'}


def safe_path(root, name):
    parts = PurePosixPath(name).parts
    if (not parts or name != '/'.join(parts) or parts[0] not in SOURCE_DIRS
            or any(p in ('.', '..') for p in parts)):
        raise ValueError(f'Invalid source path: {name}')
    path = root
    for part in parts:
        path = path / part
        if path.is_symlink():
            raise ValueError(f'Refusing symbolic link: {path}')
    return path


def fingerprint(path):
    if not path.exists():
        return None
    if not path.is_file() or path.is_symlink():
        raise ValueError(f'Expected a regular file: {path}')
    return {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'mode': stat.S_IMODE(path.stat().st_mode)}


def replace_bytes(path, data, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.overlay-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def write_json(path, value):
    replace_bytes(path, (json.dumps(value, indent=2) + '\n').encode())


def load_manifest():
    manifest = json.loads((ROOT / 'overlay-manifest.json').read_text())
    if manifest['version'] != 1 or not manifest['files']:
        raise ValueError('Unsupported or empty overlay manifest')
    for name, item in manifest['files'].items():
        if fingerprint(safe_path(ROOT, name)) != item['overlay']:
            raise ValueError(f'Overlay payload differs from its manifest: {name}')
    return manifest


def load_state(backup, manifest):
    state_file = backup / 'state.json'
    if state_file.is_symlink() or (backup / 'manifest.txt').is_symlink():
        raise ValueError('Refusing symbolic backup metadata')
    if state_file.exists():
        state = json.loads(state_file.read_text())
        if state['version'] != 1:
            raise ValueError('Unsupported backup state')
    else:
        # Preserve originals recorded by previous shell installers.
        state = {'version': 1, 'active': False, 'files': {}}
        legacy = backup / 'manifest.txt'
        if legacy.exists():
            for name in legacy.read_text().splitlines():
                if name not in manifest['files']:
                    raise ValueError(f'Unknown legacy backup path: {name}')
                original = fingerprint(safe_path(backup, name))
                marker = fingerprint(safe_path(backup, name + '.absent'))
                if (original is None) == (marker is None):
                    raise ValueError(f'Missing or ambiguous legacy backup: {name}')
                state['files'][name] = {'original': original, 'installed': None}
    for name, item in state['files'].items():
        source = safe_path(backup, name)
        if item['original'] is not None:
            if fingerprint(source) != item['original']:
                raise ValueError(f'Backup was changed or lost: {name}')
        elif fingerprint(safe_path(backup, name + '.absent')) is None:
            raise ValueError(f'Missing absence marker: {name}')
    return state


def check_target(target, manifest):
    if not (target / 'tools/game_timing.py').is_file():
        raise ValueError(f'Not a totk-mobile source directory: {target}')
    for name, expected in manifest['required'].items():
        # Read-only dependencies include core/ and requirements/.
        path = target
        for part in PurePosixPath(name).parts:
            path = path / part
            if path.is_symlink():
                raise ValueError(f'Refusing symbolic build input: {path}')
        if fingerprint(path) != expected:
            raise ValueError(f'Unsupported upstream build input: {name}\n'
                             f'Use totk-mobile commit {manifest["upstream"]["commit"]}')


def apply(target, manifest, uninstall=False, check=False):
    backup = target / '.camera_backup'
    if backup.is_symlink():
        raise ValueError(f'Refusing symbolic backup directory: {backup}')
    state = load_state(backup, manifest)
    if uninstall and not state['files']:
        raise ValueError(f'No source backups found: {backup}')
    if not uninstall:
        check_target(target, manifest)
    operations = {}
    for name in (state['files'] if uninstall else manifest['files']):
        destination = safe_path(target, name)
        actual = fingerprint(destination)
        record = state['files'].get(name)
        item = manifest['files'].get(name)
        allowed = [item['base'], item['overlay']] if item else []
        if record:
            allowed += [record['original']]
            if record['installed'] is not None:
                allowed += [record['installed']]
        if actual not in allowed:
            raise ValueError(f'Local changes would be overwritten: {name}\n'
                             'Save or move that change before applying the overlay.')
        if uninstall:
            desired = record['original']
            data = safe_path(backup, name).read_bytes() if desired else None
        else:
            desired = item['overlay']
            data = safe_path(ROOT, name).read_bytes()
        operations[name] = (actual, desired, data)
    if check:
        print(f'Compatible: {len(operations)} source files; no files changed.')
        return
    # All validation precedes writes. Retain a rollback snapshot and preserve
    # original backups across reinstalls, upgrades and legacy installations.
    before = {name: (safe_path(target, name).read_bytes() if actual else None, actual)
              for name, (actual, _, _) in operations.items()}
    metadata = {name: (backup / name).read_bytes() if (backup / name).exists() else None
                for name in ('state.json', 'manifest.txt')}
    try:
        if not uninstall:
            for name, (actual, desired, _) in operations.items():
                if name not in state['files']:
                    saved = safe_path(backup, name)
                    marker = safe_path(backup, name + '.absent')
                    if saved.exists() or marker.exists():
                        original = fingerprint(saved)
                        if original != actual or marker.exists() != (actual is None):
                            raise ValueError(f'Unrecorded backup differs: {name}')
                    elif actual:
                        replace_bytes(saved, before[name][0], actual['mode'])
                    else:
                        replace_bytes(marker, b'')
                    state['files'][name] = {'original': actual, 'installed': desired}
                else:
                    state['files'][name]['installed'] = desired
        for name, (actual, desired, data) in operations.items():
            if actual == desired:
                continue
            path = safe_path(target, name)
            if desired:
                replace_bytes(path, data, desired['mode'])
            else:
                path.unlink(missing_ok=True)
        state['active'] = not uninstall
        write_json(backup / 'state.json', state)
        replace_bytes(backup / 'manifest.txt', ('\n'.join(sorted(state['files'])) + '\n').encode())
    except Exception:
        for name, (data, previous) in before.items():
            path = safe_path(target, name)
            if previous:
                replace_bytes(path, data, previous['mode'])
            else:
                path.unlink(missing_ok=True)
        for name, data in metadata.items():
            path = backup / name
            if data is None:
                path.unlink(missing_ok=True)
            else:
                replace_bytes(path, data)
        raise
    print(f'{"Restored" if uninstall else "Installed"} {len(operations)} source files: {target}')
    print(f'Original backups retained: {backup}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('install', 'uninstall'))
    parser.add_argument('target', nargs='?', type=Path, default=ROOT.parent / 'totk-mobile')
    parser.add_argument('--check', action='store_true', help='Validate without writing files')
    args = parser.parse_args()
    try:
        apply(args.target.resolve(), load_manifest(), args.action == 'uninstall', args.check)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
