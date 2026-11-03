"""Bundle movie and offline scene variants, including pipelines restored after cutscenes."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import tempfile
import zlib

from build_shaders import ROOT, chain
from shader_pairs import MOVIE_PAIRS, name, request as request_program_pairs


def required_files(root=ROOT):
    keys = {key for vs, ps in MOVIE_PAIRS for key in (name(vs, 0, ps), vs, ps)}
    # Returning from a movie restores scene pipelines, including linked vertex
    # variants. iOS cannot invoke the Mac translator when one is first drawn.
    # Package every offline request, not only the two movie programs.
    for path in sorted((root / 'work/shaders/variants').glob('*.json')):
        request = json.loads(path.read_text())
        base, mask, fragment = request.get('base'), request.get('mask'), request.get('next')
        valid = (isinstance(base, str) and re.fullmatch(r'[0-9a-f]{16}', base) and
                 type(mask) is int and 0 <= mask <= 0xffffffff and
                 (fragment is None or isinstance(fragment, str) and re.fullmatch(r'[0-9a-f]{16}', fragment)))
        if not valid:
            raise ValueError('Invalid iOS shader request: ' + path.name)
        expected = name(base, mask, fragment) if fragment else f'{base}-{mask:04x}'
        # Vertex-only variants use eight mask digits; linked variants use a hash.
        if path.stem not in {expected, f'{base}-{mask:08x}' if fragment is None else expected}:
            raise ValueError('Shader request identity mismatch: ' + path.name)
        keys.add(path.stem)
        keys.add(base)
        if fragment:
            keys.add(fragment)
    return [key + '_b' + suffix for key in sorted(keys) for suffix in ('.metallib', '.json')]


def verify_bundle(app, root=ROOT):
    support = Path(app) / 'shader-support'
    manifest = json.loads((support / 'manifest.json').read_text())
    if (manifest.get('version') not in (1, 2) or manifest.get('chain') != chain() or
            [entry['name'] for entry in manifest['files']] != required_files(root)):
        raise RuntimeError('Stale or incomplete iOS shader support; rebuild with script/ios.sh build')
    if manifest['version'] == 2:
        with (support / 'sources.pack').open('rb') as packed:
            packedSize = (support / 'sources.pack').stat().st_size
            for entry in manifest['files']:
                offset, compressedSize = entry['offset'], entry['compressed_size']
                if (type(offset) is not int or type(compressedSize) is not int or
                        offset < 0 or compressedSize <= 0 or offset + compressedSize > packedSize or
                        not 0 < entry['size'] <= 64 << 20):
                    raise RuntimeError('Invalid packed shader bounds: ' + entry['name'])
                packed.seek(offset)
                decoder = zlib.decompressobj()
                data = decoder.decompress(packed.read(compressedSize), entry['size'] + 1)
                if (not decoder.eof or decoder.unused_data or len(data) != entry['size'] or
                        hashlib.sha256(data).hexdigest() != entry['sha256']):
                    raise RuntimeError('Invalid packed shader: ' + entry['name'])
    else:
        for entry in manifest['files']:
            data = (support / ('b-' + manifest['chain']) / entry['name']).read_bytes()
            if len(data) != entry['size'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
                raise RuntimeError('Invalid bundled shader: ' + entry['name'])
    print('Verified iOS movie shader support:', manifest['chain'], len(manifest['files']), 'files')


def install_bundle(root=ROOT):
    # TARGET_BUNDLE_DIR contains Xcode's literal ${EFFECTIVE_PLATFORM_NAME}
    # under this generator. Use the build phase environment, not that path.
    directory = os.environ.get('TARGET_BUILD_DIR')
    product = os.environ.get('FULL_PRODUCT_NAME')
    if not directory or not product or Path(product).name != product:
        raise RuntimeError('iOS support installation requires the Xcode build phase environment')
    app = Path(directory) / product
    if not app.is_dir():
        raise RuntimeError('Xcode app bundle is missing: ' + str(app))
    # Signing and device installation are expensive for 120,000 separate files.
    # Keep the same verified entries but ship their compressed bytes in one file.
    sourceRoot = root / 'work/ios/shader-support'
    manifest = json.loads((sourceRoot / 'manifest.json').read_text())
    with tempfile.TemporaryDirectory(prefix='ios-support-', dir=app.parent) as temporary:
        stagedRoot = Path(temporary)
        stagedSupport = stagedRoot / 'shader-support'
        stagedSupport.mkdir()
        shared = {}
        with (stagedSupport / 'sources.pack').open('wb') as packed:
            for entry in manifest['files']:
                identity = (entry['sha256'], entry['size'])
                if identity not in shared:
                    data = (sourceRoot / ('b-' + manifest['chain']) / entry['name']).read_bytes()
                    if len(data) != entry['size'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
                        raise RuntimeError('Invalid staged shader: ' + entry['name'])
                    compressed = zlib.compress(data, 1)
                    shared[identity] = (packed.tell(), len(compressed))
                    packed.write(compressed)
                entry['offset'], entry['compressed_size'] = shared[identity]
        manifest['version'] = 2
        (stagedSupport / 'manifest.json').write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
        verify_bundle(stagedRoot, root)
        destination = app / 'shader-support'
        if destination.exists(): shutil.rmtree(destination)
        stagedSupport.replace(destination)
        print('Packed iOS shader support:', len(manifest['files']), 'entries,',
              (destination / 'sources.pack').stat().st_size, 'bytes')


def prepare(root=ROOT):
    shaders = root / 'work/shaders'
    version = chain()
    library = shaders / ('b-' + version)
    requests = shaders / 'variants'
    requests.mkdir(parents=True, exist_ok=True)
    for vs, ps in MOVIE_PAIRS:
        (requests / (name(vs, 0, ps) + '.json')).write_text(json.dumps(dict(base=vs, mask=0, next=ps)) + '\n')
    # Enumerate after base reflection is available, so known integer input and
    # output masks are not silently omitted from a previously prepared cache.
    # Merely bundling existing captures leaves known archive programs missing.
    request_program_pairs(shaders)
    files = required_files(root)
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
            raise RuntimeError('Missing iOS shader support: ' + str(source))
        shutil.copyfile(source, target / file)
        data = source.read_bytes()
        entries.append(dict(name=file, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
    (output / 'manifest.json').write_text(json.dumps(dict(version=1, chain=version, files=entries), indent=2) + '\n')
    print('iOS movie shader support:', version, len(files), 'files')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install-bundle', action='store_true', help='Install into the actual Xcode product before signing')
    parser.add_argument('--verify-bundle', type=Path, help='Check a built app without translating shaders')
    args = parser.parse_args()
    if args.install_bundle:
        install_bundle()
    elif args.verify_bundle:
        verify_bundle(args.verify_bundle)
    else:
        prepare()
