"""Regression: short-lived connections must not retain a stack per completed thread."""
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def virtual_kib(pid):
    return int(subprocess.check_output(['ps', '-o', 'vsz=', '-p', str(pid)], text=True))


def requests(path, count):
    for _ in range(count):
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(5)
            client.connect(path)
            client.sendall(b'status\n')
            response = b''
            while not response.endswith(b'END\n'):
                chunk = client.recv(4096)
                assert chunk, 'daemon closed before responding'
                response += chunk
            assert response.startswith(b'OK status '), response


with tempfile.TemporaryDirectory(prefix='churn-', dir='/tmp') as directory:
    path = str(Path(directory) / 'broker.sock')
    daemon = subprocess.Popen([sys.argv[1], '--memory', '1MB', '--socket', path])
    held = socket.socket(socket.AF_UNIX)
    try:
        for _ in range(200):
            if Path(path).exists():
                break
            assert daemon.poll() is None, 'daemon exited during startup'
            time.sleep(0.01)
        else:
            raise TimeoutError('daemon startup')
        held.connect(path)  # A live idle session must not prevent reaping later sessions.
        requests(path, 256)  # Warm up runtime allocator and OS thread caches.
        time.sleep(0.3)
        baseline = virtual_kib(daemon.pid)
        requests(path, 8192)
        time.sleep(0.3)  # Allow an idle poll iteration to reap the final sessions.
        growth = virtual_kib(daemon.pid) - baseline
        print(f'Virtual memory growth after 8192 connections: {growth} KiB', flush=True)
        # Generous allowance for allocator noise, far below one retained stack/client.
        assert growth < 128 * 1024, f'completed thread resources retained: {growth} KiB'
        requests(path, 1)
    finally:
        held.close()
        daemon.terminate()
        try:
            daemon.wait(timeout=10)
        except subprocess.TimeoutExpired:
            daemon.kill()
            daemon.wait()
            raise
    assert daemon.returncode == 0, daemon.returncode
print('Short-lived connection churn and shutdown passed')
