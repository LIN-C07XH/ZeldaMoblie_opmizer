import json
import os
import shutil
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'script'))
import prepare_ios_shader_support as support
from shader_pairs import MOVIE_PAIRS, name


class IosShaderSupportTests(unittest.TestCase):
    def test_movie_request_and_resources_are_included_without_gameplay_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            library = root / 'work/shaders/b-test'
            library.mkdir(parents=True)
            keys = {key for vs, ps in MOVIE_PAIRS for key in (name(vs, 0, ps), vs, ps)}
            for key in keys:
                (library / (key + '_b.metallib')).write_text('converted MSL')
                (library / (key + '_b.json')).write_text('{}')
            with patch.object(support, 'chain', return_value='test'), patch.object(support, 'request_program_pairs'):
                support.prepare(root)
                support.prepare(root)
            output = root / 'work/ios/shader-support/b-test'
            self.assertEqual(len(list(output.iterdir())), 28)
            self.assertEqual(name('8c2e77dd83720228', 0, '71aa9c809617fb70'), '72fce7435f390518')
            self.assertIn('72fce7435f390518_b.metallib', support.required_files(root))
            for vs, ps in MOVIE_PAIRS:
                request = root / 'work/shaders/variants' / (name(vs, 0, ps) + '.json')
                self.assertEqual(json.loads(request.read_text()), dict(base=vs, mask=0, next=ps))
            for file in output.iterdir():
                self.assertEqual(file.read_bytes(), (library / file.name).read_bytes())
            app = root / 'ZeldaMobile.app'
            shutil.copytree(root / 'work/ios/shader-support', app / 'shader-support')
            with patch.object(support, 'chain', return_value='test'), patch.object(support, 'request_program_pairs'):
                support.verify_bundle(app, root)
                missing = app / 'shader-support/b-test/72fce7435f390518_b.metallib'
                missing.write_bytes(b'corrupt')
                with self.assertRaises(RuntimeError): support.verify_bundle(app, root)
                missing.unlink()
                with self.assertRaises(FileNotFoundError): support.verify_bundle(app, root)
                manifest = app / 'shader-support/manifest.json'
                content = json.loads(manifest.read_text()); content['files'] = content['files'][:4]
                manifest.write_text(json.dumps(content))
                with self.assertRaises(RuntimeError): support.verify_bundle(app, root)

    def test_packaging_regenerates_known_integer_variants_before_collecting_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); shaders=root/'work/shaders'; library=shaders/'b-test'
            library.mkdir(parents=True); (shaders/'source').mkdir(); (shaders/'variants').mkdir()
            vs,ps='0123456789abcdef','fedcba9876543210'
            records=[dict(key=vs,stage='vs',source='material.Product.140.product.Nin_NX_NVN.bfsha'),
                     dict(key=ps,stage='ps',source='material.Product.140.product.Nin_NX_NVN.bfsha')]
            (shaders/'index.json').write_text(json.dumps(records))
            zero,integer=name(vs,0,ps),name(vs,4096,ps)
            (shaders/'variants'/f'{zero}.json').write_text(json.dumps(dict(base=vs,mask=0,next=ps)))
            keys={key for a,b in MOVIE_PAIRS for key in (name(a,0,b),a,b)} | {vs,ps,zero,integer}
            for key in keys:
                (library/f'{key}_b.metallib').write_text('compiled source')
                (library/f'{key}_b.json').write_text(json.dumps(dict(vertex_inputs=[dict(location=6)])))
            self.assertFalse((shaders/'variants'/f'{integer}.json').exists())
            with patch.object(support,'chain',return_value='test'), patch('shader_pairs.chain',return_value='test'):
                support.prepare(root)
            self.assertEqual(json.loads((shaders/'variants'/f'{integer}.json').read_text()),dict(base=vs,mask=4096,next=ps))
            bundled=root/'work/ios/shader-support/b-test'
            self.assertTrue((bundled/f'{integer}_b.metallib').exists())
            self.assertTrue((bundled/f'{integer}_b.json').exists())

    def test_cutscene_return_variants_and_dependencies_are_bundled(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            requests = root / 'work/shaders/variants'
            requests.mkdir(parents=True)
            vs, ps = '0123456789abcdef', 'fedcba9876543210'
            key = name(vs, 4096, ps)
            (requests / (key + '.json')).write_text(json.dumps(dict(base=vs, mask=4096, next=ps)))
            required = support.required_files(root)
            for shader in (key, vs, ps):
                self.assertIn(shader + '_b.metallib', required)
                self.assertIn(shader + '_b.json', required)
            library = root / 'work/shaders/b-test'
            library.mkdir()
            for file in required:
                (library / file).write_text('offline compiled resource')
            with patch.object(support, 'chain', return_value='test'), patch.object(support, 'request_program_pairs'):
                support.prepare(root)
                app = root / 'Game.app'
                shutil.copytree(root / 'work/ios/shader-support', app / 'shader-support')
                # The actual product differs from the unexpanded CMake path.
                actual = root / 'Release-iphoneos' / 'Game.app'
                actual.mkdir(parents=True)
                with patch.dict(os.environ, TARGET_BUILD_DIR=str(actual.parent), FULL_PRODUCT_NAME=actual.name):
                    support.install_bundle(root)
                self.assertEqual({p.name for p in (actual / 'shader-support').iterdir()}, {'manifest.json', 'sources.pack'})
                support.verify_bundle(actual, root)
                packed = actual / 'shader-support/sources.pack'
                packed.write_bytes(b'broken')
                with self.assertRaises((RuntimeError, ValueError)): support.verify_bundle(actual, root)
                self.assertFalse((root / 'Release${EFFECTIVE_PLATFORM_NAME}').exists())
                support.verify_bundle(app, root)
                (app / ('shader-support/b-test/' + key + '_b.metallib')).unlink()
                with self.assertRaises(FileNotFoundError): support.verify_bundle(app, root)
            (requests / (key + '.json')).write_text(json.dumps(dict(base=vs, mask=-1, next=ps)))
            with self.assertRaises(ValueError): support.required_files(root)


if __name__ == '__main__':
    unittest.main()
