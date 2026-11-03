"""Exercise desktop lifecycle and compiler caches without touching game saves."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DesktopUpdateTests(unittest.TestCase):
    def compileFixture(self, folder, name, frameworks, extra=None):
        executable = folder / name
        arguments = ['clang', '-O1', '-g', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
                     '-Wno-deprecated-declarations', '-I', str(folder),
                     str(ROOT / 'tests' / (name + '.m'))]
        arguments += extra or []
        for framework in frameworks:
            arguments += ['-framework', framework]
        subprocess.run(arguments + ['-o', str(executable)], check=True)
        return executable

    def testCompileWorkersAreBounded(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = self.compileFixture(Path(temporary), 'test_shader_compile_policy', ['Foundation'])
            for setting, expected in [('2', '2'), ('1', '1'), ('4', '4'), ('999', '4'), ('0', '1')]:
                subprocess.run([str(executable), expected],
                               env=dict(os.environ, ZELDA_SHADER_COMPILE_JOBS=setting), check=True)

    def testMacCloudLifecycle(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = self.compileFixture(Path(temporary), 'test_mac_cloud_save', ['Cocoa'])
            subprocess.run([str(executable)], check=True)

    def testPrefetchedLibraryReuseAndGpuOutput(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = self.compileFixture(Path(temporary), 'test_nvn_library_cache', ['Foundation', 'Metal'])
            result = subprocess.run([str(executable)])
            if result.returncode == 77:
                self.skipTest('Metal device unavailable')
            self.assertEqual(result.returncode, 0)

    def testMacPipelineArchiveAcrossProcesses(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'dmsh_chain.h').write_text('#define DMSH_CHAIN "desktop-test"\n')
            (folder / 'shader_ir_identity.h').write_text('#define ZELDA_SHADER_IR_CONTRACT "desktop-test"\n')
            executable = self.compileFixture(folder, 'test_ios_pipeline_cache', ['Foundation', 'Metal'],
                                             ['-DTEST_DESKTOP_PIPELINE=1'])
            result = subprocess.run([str(executable), str(folder), 'cold'])
            if result.returncode == 77:
                self.skipTest('Metal device unavailable')
            self.assertEqual(result.returncode, 0)
            subprocess.run([str(executable), str(folder), 'warm'], check=True)
            archive = next((folder / 'work/shaders/pipeline_cache').glob('*.metallib'))
            archive.write_bytes(b'corrupt')
            subprocess.run([str(executable), str(folder), 'cold'], check=True)
            subprocess.run([str(executable), str(folder), 'warm'], check=True)
            # A valid-looking large archive must be rejected before Metal parses it.
            # The timeout makes the original startup regression fail deterministically.
            with archive.open('ab') as stream:
                stream.truncate((8 << 20) + 1)
            result = subprocess.run([str(executable), str(folder), 'cold'],
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('skip_oversized', result.stderr)
            subprocess.run([str(executable), str(folder), 'warm'], check=True, timeout=15)

    def testMacPipelineCaptureIsBounded(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'dmsh_chain.h').write_text('#define DMSH_CHAIN "desktop-bounded-test"\n')
            (folder / 'shader_ir_identity.h').write_text('#define ZELDA_SHADER_IR_CONTRACT "desktop-test"\n')
            executable = self.compileFixture(folder, 'test_ios_pipeline_cache', ['Foundation', 'Metal'],
                                             ['-DTEST_DESKTOP_PIPELINE=1'])
            result = subprocess.run([str(executable), str(folder), 'bounded'], timeout=15)
            if result.returncode == 77:
                self.skipTest('Metal device unavailable')
            self.assertEqual(result.returncode, 0)
            subprocess.run([str(executable), str(folder), 'warm'], check=True, timeout=15)
