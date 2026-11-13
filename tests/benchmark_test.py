"""Run with: python3 tests/benchmark_test.py /path/to/gpumemd"""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import time

spec = importlib.util.spec_from_file_location(
    'benchmark', Path(__file__).resolve().parents[1] / 'scripts/benchmark.py')
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)

with tempfile.TemporaryDirectory(prefix='bench-', dir='/tmp') as directory:
    sock = str(Path(directory) / 'broker.sock')
    daemon = subprocess.Popen([sys.argv[1], '--memory', '1MB', '--socket', sock])
    try:
        for _ in range(100):
            if Path(sock).exists():
                break
            assert daemon.poll() is None
            time.sleep(0.05)
        benchmark.request(sock, 'register bench 4096 test')
        # Start nonresident, then repeat independent/shared lifecycle twice.
        for _ in range(2):
            benchmark.unload_if_resident(sock, 'bench')
            benchmark.unload_if_resident(sock, 'bench')
            benchmark.request(sock, 'load bench')
            benchmark.unload_if_resident(sock, 'bench')
        benchmark.request(sock, 'load bench')
        benchmark.request(sock, 'retain bench')
        try:
            benchmark.unload_if_resident(sock, 'bench')
        except RuntimeError as error:
            assert str(error).startswith('ERR model_in_use '), str(error)
        else:
            raise AssertionError('must not suppress model_in_use')
        benchmark.request(sock, 'release_model bench')
        benchmark.unload_if_resident(sock, 'bench')
    finally:
        daemon.terminate()
        daemon.wait(timeout=10)
print('Benchmark unload lifecycle and error propagation passed')
