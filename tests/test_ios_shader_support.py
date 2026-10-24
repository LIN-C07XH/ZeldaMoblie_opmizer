import json
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
            keys = {key for vs, ps in MOVIE_PAIRS for key in (name(vs, 0, ps), ps)}
            for key in keys:
                (library / (key + '_b.metallib')).write_text('converted MSL')
                (library / (key + '_b.json')).write_text('{}')
            with patch.object(support, 'chain', return_value='test'):
                support.prepare(root)
                support.prepare(root)
            output = root / 'work/ios/shader-support/b-test'
            self.assertEqual(len(list(output.iterdir())), 24)
            self.assertEqual(name('8c2e77dd83720228', 0, '71aa9c809617fb70'), '72fce7435f390518')
            self.assertIn('72fce7435f390518_b.metallib', support.required_files())
            for vs, ps in MOVIE_PAIRS:
                request = root / 'work/shaders/variants' / (name(vs, 0, ps) + '.json')
                self.assertEqual(json.loads(request.read_text()), dict(base=vs, mask=0, next=ps))
            for file in output.iterdir():
                self.assertEqual(file.read_bytes(), (library / file.name).read_bytes())
            app = root / 'ZeldaMobile.app'
            shutil.copytree(root / 'work/ios/shader-support', app / 'shader-support')
            with patch.object(support, 'chain', return_value='test'):
                support.verify_bundle(app)
                missing = app / 'shader-support/b-test/72fce7435f390518_b.metallib'
                missing.write_bytes(b'corrupt')
                with self.assertRaises(RuntimeError): support.verify_bundle(app)
                missing.unlink()
                with self.assertRaises(FileNotFoundError): support.verify_bundle(app)
                manifest = app / 'shader-support/manifest.json'
                content = json.loads(manifest.read_text()); content['files'] = content['files'][:4]
                manifest.write_text(json.dumps(content))
                with self.assertRaises(RuntimeError): support.verify_bundle(app)


if __name__ == '__main__':
    unittest.main()
