#!/usr/bin/env python3
"""Record payload and upstream build-input fingerprints when publishing."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from overlay_package import ROOT, fingerprint, safe_path

BASE = '510d95b8da159c0a7da467ad51b10a5a7c7ac133'


def generate(upstream):
    tree = subprocess.check_output(['git', 'ls-tree', '-rz', BASE], cwd=upstream)
    entries = {}
    for row in filter(None, tree.split(b'\0')):
        info, name = row.split(b'\t', 1)
        mode, kind, oid = info.decode().split()
        if kind != 'blob':
            raise ValueError(f'Unsupported upstream entry: {name!r}')
        entries[name.decode()] = (int(mode, 8) & 0o777, oid)
    files = (ROOT / 'payload-files.txt').read_text().splitlines()
    if files != sorted(set(files)):
        raise ValueError('payload-files.txt must be sorted and unique')
    required = {p for p in entries if p.split('/')[0] in
                {'core', 'host', 'ios', 'script', 'tools', 'requirements'} and p not in files}
    base = {}
    for name in sorted(set(files) | required):
        if name not in entries:
            base[name] = None
            continue
        mode, oid = entries[name]
        data = subprocess.check_output(['git', 'cat-file', 'blob', oid], cwd=upstream)
        base[name] = {'sha256': hashlib.sha256(data).hexdigest(), 'mode': mode}
    manifest = {'version': 1,
                'upstream': {'url': 'https://github.com/OJZen/totk-mobile.git', 'commit': BASE},
                'game_version': '1.4.3',
                'files': {p: {'base': base[p], 'overlay': fingerprint(safe_path(ROOT, p))} for p in files},
                'required': {p: base[p] for p in sorted(required)}}
    if any(v['overlay'] is None for v in manifest['files'].values()):
        raise ValueError('Missing overlay payload')
    (ROOT / 'overlay-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Recorded {len(files)} payload files and {len(required)} upstream build inputs.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream', type=Path, help='Upstream Git checkout containing the pinned base')
    generate(parser.parse_args().upstream)
