import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'script'))
import prepare_ios_shader_ir as shaderIr


class IosShaderIrTests(unittest.TestCase):
    def test_runtime_loader_checks_source_binary_bounds_and_policy(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'shader_ir_identity.h').write_text('#define IOS_SHADER_IR_CONTRACT "test"\n#define ZELDA_SHADER_IR_CONTRACT "unused"\n')
            executable = folder / 'ir_loader'
            subprocess.run(['clang', '-O1', '-g', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', '-Wno-deprecated-declarations', '-I', str(folder),
                            str(ROOT / 'tests/test_ios_shader_ir_loader.m'), '-framework', 'Foundation',
                            '-framework', 'Metal', '-o', str(executable)], check=True)
            packed = folder / 'shader-ir'
            packed.mkdir()
            (packed / 'libraries.pack').write_bytes(b'MTLBfixture')
            entry = dict(offset=0, size=11, source_sha256=hashlib.sha256(b'fixture source').hexdigest(),
                         binary_sha256=hashlib.sha256(b'MTLBfixture').hexdigest())
            manifest = dict(version=1, contract='test', target=shaderIr.TARGET, language=shaderIr.LANGUAGE,
                            source_directory='b-test', files={'a.metallib': entry})
            for field, value in [(None, None), ('offset', 2**64-1), ('size', 2**64-1),
                                 ('source_sha256', 'changed'), ('binary_sha256', 'corrupt')]:
                changed = json.loads(json.dumps(manifest))
                if field: changed['files']['a.metallib'][field] = value
                (packed / 'manifest.json').write_text(json.dumps(changed))
                subprocess.run([str(executable), str(folder), 'invalid' if field else 'valid'], check=True)

    def test_pack_validation_shared_ranges_corruption_and_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            binary = b'MTLBfixture'
            (folder / 'libraries.pack').write_bytes(binary)
            entry = dict(offset=0, size=len(binary), binary_sha256=hashlib.sha256(binary).hexdigest(), source_sha256='source')
            manifest = dict(version=1, contract='test', target=shaderIr.TARGET, language=shaderIr.LANGUAGE,
                            files={'a.metallib': entry, 'b.metallib': dict(entry)})
            manifestPath = folder / 'manifest.json'
            manifestPath.write_text(json.dumps(manifest))
            self.assertEqual(len(shaderIr.verifyIosIr(folder, 'test')['files']), 2)
            with self.assertRaises(ValueError): shaderIr.verifyIosIr(folder, 'other')
            for offset, size in [(-1, 4), (0, -1), (2**64, 4), (0, 2**64), (True, 4)]:
                entry.update(offset=offset, size=size)
                manifestPath.write_text(json.dumps(manifest))
                with self.assertRaises(ValueError): shaderIr.verifyIosIr(folder, 'test')
            entry.update(offset=0, size=len(binary))
            manifestPath.write_text(json.dumps(manifest))
            (folder / 'libraries.pack').write_bytes(b'MTLBbroken!')
            with self.assertRaises(ValueError): shaderIr.verifyIosIr(folder, 'test')

    def test_missing_toolchain_leaves_existing_pack_untouched(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'work/shaders/b-test'
            source.mkdir(parents=True)
            (source / 'a.metallib').write_text('source')
            output = root / 'work/ios/shader-ir'
            output.mkdir(parents=True)
            (output / 'libraries.pack').write_bytes(b'previous')
            with patch('build_shaders.chain', return_value='test'), patch.object(shaderIr, 'runCommand', side_effect=RuntimeError('missing toolchain')):
                with self.assertRaises(RuntimeError): shaderIr.prepareIosIr(root)
            self.assertEqual((output / 'libraries.pack').read_bytes(), b'previous')

    def test_offline_ios_compiler_deduplicates_and_reuses_real_libraries(self):
        try:
            shaderIr.runCommand(['xcrun', '--sdk', 'iphoneos', 'metal', '--version'])
        except RuntimeError:
            self.skipTest('Optional iPhoneOS Metal Toolchain is not installed')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('tools/shader_library_probe.m', 'host/nvn_shader_math.h', 'host/nvn_cube_source.h'):
                destination = root / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, destination)
            sources = root / 'work/shaders/b-test'
            sources.mkdir(parents=True)
            source = '#include <metal_stdlib>\nusing namespace metal;\n' + \
                'template<typename T> [[clang::optnone]] T spvFAdd(T a, T b) { return a+b; }\n' + \
                'kernel void fixture(device float *out [[buffer(0)]]) { out[0] = spvFAdd(1.0f, 2.0f); }\n'
            for name in ('a.metallib', 'b.metallib'):
                (sources / name).write_text(source)
            with patch('build_shaders.chain', return_value='test'):
                shaderIr.prepareIosIr(root)
                output = root / 'work/ios/shader-ir'
                manifest = shaderIr.verifyCompleteIosIr(output, root)
                self.assertEqual(manifest['target'], 'air64-apple-ios17.0')
                self.assertEqual(manifest['files']['a.metallib']['offset'], manifest['files']['b.metallib']['offset'])
                cache = next((root / 'work/ios/shader-ir-cache').glob('*/*.metallib'))
                before = cache.stat().st_mtime_ns
                shaderIr.prepareIosIr(root)
                self.assertEqual(before, cache.stat().st_mtime_ns)
                (sources / 'new.metallib').write_text(source)
                with self.assertRaises(ValueError): shaderIr.verifyCompleteIosIr(output, root)

    def test_pipeline_archive_cross_process_gpu_and_corruption_recovery(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'dmsh_chain.h').write_text('#define DMSH_CHAIN "test"\n')
            (folder / 'shader_ir_identity.h').write_text('#define IOS_SHADER_IR_CONTRACT "test"\n')
            executable = folder / 'pipeline_test'
            subprocess.run(['clang', '-O1', '-g', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
                            '-Wno-deprecated-declarations', '-I', str(folder),
                            str(ROOT / 'tests/test_ios_pipeline_cache.m'), '-framework', 'Foundation',
                            '-framework', 'Metal', '-o', str(executable)], check=True)
            result = subprocess.run([str(executable), str(folder), 'cold'])
            if result.returncode == 77:
                self.skipTest('Metal GPU is unavailable in this execution environment')
            self.assertEqual(result.returncode, 0)
            subprocess.run([str(executable), str(folder), 'warm'], check=True)
            archive = next((folder / 'work/shaders/pipeline_cache').glob('*.metallib'))
            archive.write_bytes(b'corrupt')
            subprocess.run([str(executable), str(folder), 'cold'], check=True)
            subprocess.run([str(executable), str(folder), 'warm'], check=True)


if __name__ == '__main__':
    unittest.main()
