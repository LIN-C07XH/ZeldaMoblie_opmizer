from pathlib import Path
import json
import os
import signal
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'script'))
import run_session


class SessionTests(unittest.TestCase):
    def run_child(self, folder, code):
        with patch.object(run_session, 'memory_sampler', return_value=None):
            status = run_session.run([sys.executable, '-c', code], folder, interval=0.01)
        latest = (folder / 'latest-session').resolve()
        return status, latest, json.loads((latest / 'exit.json').read_text())

    def test_runs_preserve_previous_logs_and_collect_per_run_performance(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'work/tmp') as temporary:
            folder = Path(temporary)
            (folder / 'native-probe.log').write_text('legacy log\n')
            (folder / 'performance.tsv').write_text('legacy performance\n')
            code = 'import os; from pathlib import Path; print("first run", flush=True); Path(os.environ["ZELDA_PERFORMANCE_LOG"]).write_text("fps\\n60\\n")'
            status, first, details = self.run_child(folder, code)
            self.assertEqual(status, 0)
            self.assertEqual(details['cause'], 'exit')
            self.assertEqual((first / 'previous-runtime.log').read_text(), 'legacy log\n')
            self.assertEqual((first / 'previous-performance.tsv').read_text(), 'legacy performance\n')
            self.assertEqual((folder / 'performance.tsv').read_text(), 'fps\n60\n')
            status, second, _ = self.run_child(folder, 'print("second run", flush=True)')
            self.assertEqual(status, 0)
            self.assertNotEqual(first, second)
            self.assertIn('first run', (first / 'runtime.log').read_text())
            self.assertIn('second run', (folder / 'native-probe.log').read_text())
            self.assertTrue((second / 'memory.jsonl').read_text())

    def test_forced_termination_is_recorded_without_claiming_memory_pressure(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'work/tmp') as temporary:
            status, folder, details = self.run_child(Path(temporary),
                'import os, signal; print("before kill", flush=True); os.kill(os.getpid(), signal.SIGKILL)')
            self.assertEqual(status, 128 + signal.SIGKILL)
            self.assertEqual(details['signal'], 'SIGKILL')
            self.assertFalse(details['timeout'])
            self.assertIn('not established', details['note'])
            self.assertIn('before kill', (folder / 'runtime.log').read_text())

    def test_native_alarm_exit_is_distinguished_from_a_crash(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'work/tmp') as temporary:
            status, _, details = self.run_child(Path(temporary), 'import os; os._exit(142)')
            self.assertEqual(status, 142)
            self.assertEqual(details['signal'], 'SIGALRM')
            self.assertTrue(details['timeout'])


if __name__ == '__main__':
    unittest.main()
