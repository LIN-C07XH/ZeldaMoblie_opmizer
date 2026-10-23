"""Preserve each desktop run, its memory samples, and its observed exit status."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def point_latest(link, target):
    temporary = link.with_name(link.name + '.new-' + str(os.getpid()))
    temporary.symlink_to(os.path.relpath(target, link.parent))
    temporary.replace(link)


def exit_details(returncode):
    # The native fault handler uses _exit(128 + signal); uncaught signals are negative in Popen.
    caught = {signal.SIGBUS, signal.SIGSEGV, signal.SIGILL, signal.SIGTRAP, signal.SIGALRM}
    number = -returncode if returncode < 0 else returncode - 128 if returncode - 128 in caught else None
    name = signal.Signals(number).name if number else None
    return {'returncode': returncode, 'signal': name, 'timeout': number == signal.SIGALRM,
            'cause': 'timeout' if number == signal.SIGALRM else 'signal' if number else 'exit',
            'shell_status': 128 + number if returncode < 0 else returncode}


def memory_sampler():
    if sys.platform != 'darwin':
        return None
    from run_guarded import MacMemory
    return MacMemory()


def run(command, log_root, interval=1.0):
    log_root = Path(log_root).resolve()
    sessions = log_root / 'sessions'
    sessions.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    folder = sessions / (stamp + '-' + str(os.getpid()))
    folder.mkdir()
    runtime = folder / 'runtime.log'
    runtime.touch()
    # Keep a legacy log before replacing the well-known latest-run shortcut.
    latest = log_root / 'native-probe.log'
    if latest.exists() and not latest.is_symlink():
        latest.replace(folder / 'previous-runtime.log')
    point_latest(latest, runtime)
    point_latest(log_root / 'latest-session', folder)
    performance = folder / 'performance.tsv'
    previous_performance = log_root / 'performance.tsv'
    if previous_performance.exists() and not previous_performance.is_symlink():
        previous_performance.replace(folder / 'previous-performance.tsv')
    performance.touch()
    point_latest(previous_performance, performance)
    environment = dict(os.environ, ZELDA_PERFORMANCE_LOG=str(performance))
    started = time.monotonic()
    info = {'started_utc': datetime.now(timezone.utc).isoformat(), 'command': list(command),
            'timeout_seconds': environment.get('ZELDA_TIMEOUT_SECONDS', '120'), 'cwd': str(Path.cwd())}
    write_json(folder / 'session.json', info)
    print('Session logs: ' + str(folder), flush=True)
    try:
        sampler = memory_sampler()
    except Exception as error:
        sampler = None
        info['sampler_error'] = type(error).__name__
    peak = 0
    last_pressure = None
    previous_handlers = {}
    with runtime.open('ab', buffering=0) as output, (folder / 'memory.jsonl').open('w', buffering=1) as samples:
        try:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                       env=environment, start_new_session=True)
        except OSError as error:
            write_json(folder / 'exit.json', {'cause': 'launch_error', 'error': str(error)})
            return 127
        info['pid'] = process.pid
        write_json(folder / 'session.json', info)

        def forward(number, frame):
            del frame
            try:
                os.kill(process.pid, number)
            except ProcessLookupError:
                pass

        for number in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            previous_handlers[number] = signal.signal(number, forward)
        try:
            while process.poll() is None:
                sample = {'elapsed_s': round(time.monotonic() - started, 3), 'pid': process.pid}
                if sampler:
                    try:
                        size = sampler.group_bytes(process.pid)
                        last_pressure = sampler.pressure()
                        peak = max(peak, size)
                        sample.update(physical_bytes=size, pressure_level=last_pressure)
                    except (OSError, RuntimeError) as error:
                        sample['error'] = type(error).__name__
                samples.write(json.dumps(sample) + '\n')
                try:
                    process.wait(timeout=interval)
                except subprocess.TimeoutExpired:
                    pass
        finally:
            for number, handler in previous_handlers.items():
                signal.signal(number, handler)
        result = exit_details(process.returncode)
        result.update(ended_utc=datetime.now(timezone.utc).isoformat(),
                      elapsed_s=round(time.monotonic() - started, 3),
                      peak_physical_bytes=peak, last_pressure_level=last_pressure)
        # SIGKILL identifies a forced termination; only OS diagnostics can attribute it to memory pressure.
        if result['signal'] == 'SIGKILL':
            result['note'] = 'Forced termination; memory pressure is not established by SIGKILL alone.'
        write_json(folder / 'exit.json', result)
    print('Session ended: ' + json.dumps(result), flush=True)
    return result['shell_status']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log-root', default='work/logs')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('a command is required')
    raise SystemExit(run(command, args.log_root))
