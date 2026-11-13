#!/usr/bin/env python3
"""Validate acquire/release via separate CLI processes; reservations are logical, not cudaMalloc."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--build', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    capacity, amount = 1024**3, 64 * 1024**2
    with tempfile.TemporaryDirectory(prefix='gpumemd-acquire-', dir='/tmp') as directory:
        sock = Path(directory) / 'broker.sock'
        client = [str(build / 'gpumemctl'), '--socket', str(sock)]
        def request(*command, expected='OK'):
            result = subprocess.run(client + list(map(str, command)), text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
            assert result.stdout.startswith(expected + ' '), result.stdout
            assert (result.returncode == 0) == (expected == 'OK'), result.stdout
            return result.stdout

        daemon = subprocess.Popen([str(build / 'gpumemd'), '--memory', str(capacity),
                                   '--socket', str(sock)], stdout=subprocess.DEVNULL)
        waiter = None
        try:
            for _ in range(200):
                if sock.exists():
                    break
                if daemon.poll() is not None:
                    raise RuntimeError('daemon exited before readiness')
                time.sleep(0.05)
            else:
                raise TimeoutError('daemon not ready')

            request('acquire', 'probe', amount)
            status = request('status')
            assert status.splitlines()[0] == f'OK status {capacity} {amount} {capacity-amount} 1'
            request('acquire', 'probe', amount, expected='ERR duplicate_client')
            request('release', 'probe')
            request('acquire', 'oversized', capacity + 1, expected='ERR insufficient_memory')
            request('acquire', 'holder', capacity)
            request('try_acquire', 'no_room', amount, expected='ERR insufficient_memory')
            start = time.monotonic()
            request('acquire', 'timed', amount, 0, 200, expected='ERR timeout')
            timeout_ms = (time.monotonic() - start) * 1000
            assert timeout_ms >= 180, f'timeout returned too early: {timeout_ms} ms'

            waiter = subprocess.Popen(client + ['acquire', 'waiter', str(amount), '0', '5000'],
                                      text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                output, _ = waiter.communicate(timeout=0.2)
            except subprocess.TimeoutExpired:
                pass
            else:
                raise AssertionError(f'waiter completed before release: {output}')
            request('release', 'holder')
            output, _ = waiter.communicate(timeout=10)
            assert waiter.returncode == 0 and output == f'OK acquired waiter {amount}\n', output
            request('release', 'waiter')
            status = request('status')
            assert status == f'OK status {capacity} 0 {capacity} 0\nEND\n', status
            raw_metrics = request('metrics')
            metrics = {key: int(value) for key, value in
                       (line.split() for line in raw_metrics.splitlines()[1:-1])}
            assert metrics['acquires'] == 7, metrics
            assert metrics['releases'] == 3, metrics
            assert metrics['acquired_bytes'] == metrics['released_bytes'] == capacity + 2 * amount, metrics
            print(json.dumps(dict(result='PASS', checks=[
                'acquire_and_accounting', 'duplicate_rejected', 'oversized_rejected',
                'try_acquire_full', 'timeout', 'waiter_unblocked_by_release',
                'zero_final_reservations', 'balanced_byte_counters'],
                observed_timeout_ms=timeout_ms, metrics=metrics, final_status=status), indent=2))
        finally:
            if waiter is not None and waiter.poll() is None:
                waiter.kill()
                waiter.communicate()
            daemon.terminate()
            try:
                daemon.wait(timeout=10)
            except subprocess.TimeoutExpired:
                daemon.kill()
                daemon.wait()


if __name__ == '__main__':
    main()
