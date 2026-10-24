"""Bundle verified movie interface variants so iOS never needs the Mac translator for them."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

from build_shaders import ROOT, chain
from shader_pairs import MOVIE_PAIRS, name


def required_files():
    keys = {key for vs, ps in MOVIE_PAIRS for key in (name(vs, 0, ps), ps)}
    return [key + '_b' + suffix for key in sorted(keys) for suffix in ('.metallib', '.json')]


def verify_bundle(app):
    support = Path(app) / 'shader-support'
    manifest = json.loads((support / 'manifest.json').read_text())
    if (manifest.get('version') != 1 or manifest.get('chain') != chain() or
            [entry['name'] for entry in manifest['files']] != required_files()):
        raise RuntimeError('Stale or incomplete iOS movie shader support; rebuild with script/ios.sh build')
    for entry in manifest['files']:
        data = (support / ('b-' + manifest['chain']) / entry['name']).read_bytes()
        if len(data) != entry['size'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise RuntimeError('Invalid bundled movie shader: ' + entry['name'])
    print('Verified iOS movie shader support:', manifest['chain'], len(manifest['files']), 'files')


def prepare(root=ROOT):
    shaders = root / 'work/shaders'
    version = chain()
    library = shaders / ('b-' + version)
    requests = shaders / 'variants'
    requests.mkdir(parents=True, exist_ok=True)
    for vs, ps in MOVIE_PAIRS:
        (requests / (name(vs, 0, ps) + '.json')).write_text(json.dumps(dict(base=vs, mask=0, next=ps)) + '\n')
    files = required_files()
    if any(not (library / file).is_file() for file in files):
        from compile_runtime_shaders import compile_pending
        compile_pending()
    output = root / 'work/ios/shader-support'
    if output.exists():
        shutil.rmtree(output)
    target = output / ('b-' + version)
    target.mkdir(parents=True)
    entries = []
    for file in files:
        source = library / file
        if not source.is_file() or not source.stat().st_size:
            raise RuntimeError('Missing iOS movie shader support: ' + str(source))
        shutil.copyfile(source, target / file)
        data = source.read_bytes()
        entries.append(dict(name=file, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
    (output / 'manifest.json').write_text(json.dumps(dict(version=1, chain=version, files=entries), indent=2) + '\n')
    print('iOS movie shader support:', version, len(files), 'files')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-bundle', type=Path, help='Check a built app without translating shaders')
    args = parser.parse_args()
    if args.verify_bundle:
        verify_bundle(args.verify_bundle)
    else:
        prepare()
