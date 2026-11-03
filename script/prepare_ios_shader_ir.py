"""Compile iOS 17+ Metal libraries on the Mac and pack them before app signing."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TARGET = 'air64-apple-ios17.0'
LANGUAGE = 'metal3.0'
STRICT_FLAGS = ['-fno-fast-math']
CUBES = {'48e3901f9b4eb169_b', 'c3960b272d349125_b'}


@lru_cache(maxsize=1)
def iosIrContract():
    digest = hashlib.sha256(json.dumps([1, TARGET, LANGUAGE, STRICT_FLAGS]).encode())
    for name in ('host/nvn_shader_math.h', 'host/nvn_cube_source.h', 'tools/shader_library_probe.m'):
        digest.update(name.encode() + b'\0' + (ROOT / name).read_bytes())
    return digest.hexdigest()


def runCommand(arguments):
    environment = dict(os.environ)
    if environment.get('ZELDA_IOS_METAL_DEVELOPER_DIR'):
        environment['DEVELOPER_DIR'] = environment['ZELDA_IOS_METAL_DEVELOPER_DIR']
    result = subprocess.run([str(value) for value in arguments], env=environment,
                            capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(result.stderr[-4000:])
    return result.stdout.strip()


def verifyIosIr(folder, expectedContract=None, expectedNames=None):
    manifest = json.loads((folder / 'manifest.json').read_text())
    if (manifest.get('version') != 1 or manifest.get('target') != TARGET or
            manifest.get('language') != LANGUAGE or manifest.get('contract') != (expectedContract or iosIrContract()) or
            not isinstance(manifest.get('files'), dict) or not manifest['files']):
        raise ValueError('Invalid or stale iOS shader IR manifest')
    if expectedNames is not None and set(manifest['files']) != set(expectedNames):
        raise ValueError('Incomplete iOS shader IR; rebuild before installing')
    verified = set()
    with (folder / 'libraries.pack').open('rb') as stream:
        packedSize = os.fstat(stream.fileno()).st_size
        for name, entry in manifest['files'].items():
            offset, size = entry['offset'], entry['size']
            if (Path(name).name != name or not name.endswith('.metallib') or
                    type(offset) is not int or type(size) is not int or offset < 0 or
                    not 4 <= size <= 64 << 20 or offset > packedSize or size > packedSize - offset or
                    not isinstance(entry.get('source_sha256'), str)):
                raise ValueError('Invalid iOS shader IR entry: ' + name)
            identity = (offset, size, entry['binary_sha256'])
            if identity in verified:
                continue
            stream.seek(offset)
            data = stream.read(size)
            if data[:4] != b'MTLB' or hashlib.sha256(data).hexdigest() != entry['binary_sha256']:
                raise ValueError('Corrupt iOS shader IR: ' + name)
            verified.add(identity)
    return manifest


def prepareIosIr(root=ROOT, jobs=2, keys=None):
    from build_shaders import chain
    version = chain()
    sourceDirectory = root / 'work/shaders' / ('b-' + version)
    sources = sorted(sourceDirectory.glob('*.metallib'))
    if keys is not None:
        selected = set(keys)
        sources = [path for path in sources if path.name in selected]
        if selected != {path.name for path in sources}:
            raise ValueError('Unknown iOS shader IR source')
    if not sources:
        raise ValueError('No prepared shader sources')
    # Fail before publishing any bundle resources if the optional toolchain is absent.
    compiler = runCommand(['xcrun', '--sdk', 'iphoneos', '--find', 'metal'])
    runCommand([compiler, '--version'])
    sdk = runCommand(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'])
    contract = iosIrContract()
    cache = root / 'work/ios/shader-ir-cache' / contract
    cache.mkdir(parents=True, exist_ok=True)
    output = root / ('work/ios/shader-ir' if keys is None else 'work/ios/shader-ir-probe')
    with tempfile.TemporaryDirectory(prefix='ios-ir-', dir=output.parent) as temporary:
        temporary = Path(temporary)
        exporter = temporary / 'exporter'
        runCommand(['clang', '-O2', '-fobjc-arc', root / 'tools/shader_library_probe.m',
                    '-framework', 'Foundation', '-framework', 'Metal', '-framework', 'QuartzCore', '-o', exporter])
        tasks, identities = {}, {}
        for source in sources:
            data = source.read_bytes()
            sourceHash = hashlib.sha256(data).hexdigest()
            for layered in ((False, True) if source.stem in CUBES else (False,)):
                name = source.stem + ('-cube' if layered else '') + '.metallib'
                identity = sourceHash + ('-cube' if layered else '')
                identities[name] = (identity, sourceHash)
                tasks.setdefault(identity, (source, layered))

        def compileLibrary(item):
            identity, (source, layered) = item
            binary, receipt = cache / (identity + '.metallib'), cache / (identity + '.json')
            if binary.is_file() and receipt.is_file():
                data = binary.read_bytes()
                try:
                    record = json.loads(receipt.read_text())
                    if data[:4] == b'MTLB' and record['binary_sha256'] == hashlib.sha256(data).hexdigest():
                        return identity, binary, record['binary_sha256']
                except (KeyError, ValueError, TypeError):
                    pass
            with tempfile.TemporaryDirectory(dir=temporary) as jobDirectory:
                jobDirectory = Path(jobDirectory)
                prepared, compiled = jobDirectory / 'source.metal', jobDirectory / 'output.metallib'
                exported = json.loads(runCommand([exporter, 'export', source, prepared, *(['cube'] if layered else [])]))
                flags = STRICT_FLAGS if exported['strict_math'] else []
                runCommand([compiler, '-isysroot', sdk, '-target', TARGET, '-std=' + LANGUAGE,
                            '-fmodules-cache-path=' + str(cache / 'module_cache'), *flags, prepared, '-o', compiled])
                data = compiled.read_bytes()
                if data[:4] != b'MTLB':
                    raise ValueError('Invalid compiled iOS shader: ' + source.name)
                binaryHash = hashlib.sha256(data).hexdigest()
                stagedReceipt = jobDirectory / 'receipt.json'
                stagedReceipt.write_text(json.dumps(dict(binary_sha256=binaryHash)))
                compiled.replace(binary)
                stagedReceipt.replace(receipt)
                return identity, binary, binaryHash

        entries, locations = {}, {}
        staged = temporary / 'shader-ir'
        staged.mkdir()
        with (staged / 'libraries.pack').open('wb') as packed, ThreadPoolExecutor(max_workers=jobs) as executor:
            for count, (identity, binary, binaryHash) in enumerate(executor.map(compileLibrary, tasks.items()), 1):
                data = binary.read_bytes()
                locations[identity] = dict(offset=packed.tell(), size=len(data), binary_sha256=binaryHash)
                packed.write(data)
                if count % 100 == 0 or count == len(tasks):
                    print(f'IOS_SHADER_IR unique={count}/{len(tasks)} libraries={len(identities)}', flush=True)
        for name, (identity, sourceHash) in identities.items():
            entries[name] = dict(locations[identity], source_sha256=sourceHash)
        manifest = dict(version=1, contract=contract, target=TARGET, language=LANGUAGE,
                        source_directory=sourceDirectory.name, files=entries)
        (staged / 'manifest.json').write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
        verifyIosIr(staged)
        # Old output remains usable until compilation and verification both succeed.
        if output.exists():
            shutil.rmtree(output)
        staged.replace(output)


def installIosIr(root=ROOT):
    output = root / 'work/ios/shader-ir'
    verifyCompleteIosIr(output, root)
    product = os.environ['FULL_PRODUCT_NAME']
    if Path(product).name != product:
        raise ValueError('Invalid iOS app product name')
    app = Path(os.environ['TARGET_BUILD_DIR']) / product
    destination = app / 'shader-ir'
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(output, destination)
    verifyCompleteIosIr(destination, root)


def verifyCompleteIosIr(folder, root=ROOT):
    from build_shaders import chain
    sourceDirectory = root / 'work/shaders' / ('b-' + chain())
    expectedNames = {source.name for source in sourceDirectory.glob('*.metallib')}
    expectedNames.update(name + '-cube.metallib' for name in CUBES if name + '.metallib' in expectedNames)
    if not expectedNames:
        raise ValueError('Missing iOS shader sources')
    manifest = verifyIosIr(folder, expectedNames=expectedNames)
    if manifest.get('source_directory') != sourceDirectory.name:
        raise ValueError('Stale iOS shader IR source directory')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, choices=(1, 2, 4), default=2)
    parser.add_argument('--install-bundle', action='store_true')
    parser.add_argument('--verify-bundle', type=Path)
    parser.add_argument('--keys', type=Path, help='Diagnostic subset; normal builds compile every source')
    args = parser.parse_args()
    if args.install_bundle:
        installIosIr()
    elif args.verify_bundle:
        verifyCompleteIosIr(args.verify_bundle / 'shader-ir')
    else:
        prepareIosIr(jobs=args.jobs, keys=args.keys.read_text().splitlines() if args.keys else None)
