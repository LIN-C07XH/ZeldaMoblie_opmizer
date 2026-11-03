import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class IOSConfigTests(unittest.TestCase):
    def configure(self, metal=None, target=None, sdk=None):
        environment = {k: v for k, v in os.environ.items()
                       if k not in ('ZELDA_IOS_METAL', 'ZELDA_IOS_DEPLOYMENT_TARGET')}
        if metal is not None:
            environment['ZELDA_IOS_METAL'] = metal
        if target is not None:
            environment['ZELDA_IOS_DEPLOYMENT_TARGET'] = target
        command = 'source script/ios_config.sh; ios_configure || exit $?; '
        if sdk is not None:
            command += f'xcrun() {{ printf "%s\\n" "{sdk}"; }}; ios_check_sdk || exit $?; '
        command += 'printf "%s\\n" "$ZELDA_IOS_METAL" "$ZELDA_IOS_DEPLOYMENT_TARGET" "$ios_build_dir" "$ios_deps_prefix"'
        return subprocess.run(['bash', '-c', command], cwd=ROOT, env=environment,
                              text=True, capture_output=True)

    def test_defaults_and_target_isolation(self):
        default = self.configure().stdout.splitlines()
        ios17 = self.configure('3').stdout.splitlines()
        ios18 = self.configure('3', '18.0').stdout.splitlines()
        self.assertEqual(default[:2], ['4', '26.0'])
        self.assertEqual(ios17[:2], ['3', '17.0'])
        self.assertEqual(ios18[:2], ['3', '18.0'])
        self.assertEqual(len({default[2], ios17[2], ios18[2]}), 3)
        self.assertEqual(len({default[3], ios17[3], ios18[3]}), 3)

    def test_rejects_unsupported_combinations_before_building(self):
        for metal, target in [('2', None), ('4', '18.0'), ('3', '16.0'),
                              ('3', '../17.0'), ('3', '17.0;exit 0')]:
            with self.subTest(metal=metal, target=target):
                self.assertEqual(self.configure(metal, target).returncode, 2)

    def test_sdk_must_supply_headers_and_cover_the_deployment_target(self):
        for metal, target, sdk, success in [('3', '17.0', '18.2', True),
                                           ('3', '18.0', '18.2', True),
                                           ('3', '17.0', '17.5', False),
                                           ('3', '18.3', '18.2', False),
                                           ('4', '26.0', '18.2', False),
                                           ('4', '26.0', '26.0', True)]:
            with self.subTest(metal=metal, target=target, sdk=sdk):
                self.assertEqual(self.configure(metal, target, sdk).returncode == 0, success)

    def test_install_selects_the_matching_app(self):
        with tempfile.TemporaryDirectory() as temp:
            fixtureRoot = Path(temp).resolve() / 'project'
            (fixtureRoot / 'script').mkdir(parents=True)
            (fixtureRoot / 'work/venv/bin').mkdir(parents=True)
            for scriptName in ('ios.sh', 'ios_config.sh'):
                (fixtureRoot / 'script' / scriptName).write_bytes((ROOT / 'script' / scriptName).read_bytes())
            validator = fixtureRoot / 'work/venv/bin/python'
            validator.write_text('#!/bin/sh\nexit 0\n')
            validator.chmod(0o755)
            executable = Path(temp) / 'xcrun'
            executable.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            executable.chmod(0o755)
            environment = dict(os.environ, ZELDA_IOS_METAL='3',
                               ZELDA_IOS_DEPLOYMENT_TARGET='17.0',
                               ZELDA_IOS_DEVICE='test-device', PATH=temp + ':' + os.environ['PATH'])
            result = subprocess.run(['bash', 'script/ios.sh', 'install'], cwd=fixtureRoot,
                                    env=environment, text=True, capture_output=True, check=True)
            self.assertEqual(result.stdout.splitlines(), ['devicectl', 'device', 'install', 'app',
                             '--device', 'test-device',
                             str(fixtureRoot / 'work/ios/build-metal3-ios17.0/Release-iphoneos/ZeldaMobile.app')])


if __name__ == '__main__':
    unittest.main()
