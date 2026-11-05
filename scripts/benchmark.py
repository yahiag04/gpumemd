#!/usr/bin/env python3
"""Socket latency or CUDA multiprocess memory experiment; uses an existing daemon."""
import argparse
import json
import math
import socket
import selectors
import subprocess
import time


def request(path, command):
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(30)
        connection.connect(path)
        connection.sendall((command + '\n').encode())
        result = b''
        while not result.endswith(b'\n'):
            chunk = connection.recv(4096)
            if not chunk:
                raise RuntimeError('unexpected EOF')
            result += chunk
        if not result.startswith(b'OK'):
            raise RuntimeError(result.decode())
        return result.decode()


def read_line(worker):
    with selectors.DefaultSelector() as selector:
        selector.register(worker.stdout, selectors.EVENT_READ)
        if not selector.select(120):
            raise TimeoutError("worker timed out")
        return worker.stdout.readline().strip()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--socket', required=True)
    parser.add_argument('--model', required=True)
    parser.add_argument('--samples', type=int, default=10000)
    parser.add_argument('--cuda-worker')
    parser.add_argument('--file')
    parser.add_argument('--processes', type=int, default=4)
    args = parser.parse_args()
    if args.samples < 1 or args.processes < 1:
        parser.error('samples and processes must be positive')
    if any(c.isspace() for c in args.model):
        parser.error('model must be a single token')
    if not args.cuda_worker:
        request(args.socket, 'load ' + args.model)
        for _ in range(1000):
            request(args.socket, 'load ' + args.model)
        samples = []
        for _ in range(args.samples):
            start = time.perf_counter_ns()
            request(args.socket, 'load ' + args.model)
            samples.append(time.perf_counter_ns() - start)
        samples.sort()
        print(json.dumps({'samples': len(samples), **{
            f'p{q}_ns': samples[math.ceil(q / 100 * len(samples)) - 1]
            for q in (50, 95, 99)}}))
        return
    if not args.file:
        parser.error('--file required for CUDA comparison')
    def memory():
        return int(subprocess.check_output([
            'nvidia-smi', '-i', '0', '--query-gpu=memory.used',
            '--format=csv,noheader,nounits'], text=True).strip())
    # Dedicated idle GPU required. Keep identical initialized worker contexts in both cases.
    for mode in ('independent', 'shared'):
        request(args.socket, 'unload ' + args.model)
        workers = []
        retained = False
        try:
            for _ in range(args.processes):
                worker = subprocess.Popen([args.cuda_worker, mode, args.file,
                                           args.socket, args.model], stdin=subprocess.PIPE,
                                          stdout=subprocess.PIPE, text=True)
                workers.append(worker)
                if read_line(worker) != 'ready':
                    raise RuntimeError('worker initialization failed')
            time.sleep(1)
            baseline = memory()
            start = time.perf_counter_ns()
            if mode == 'shared':
                request(args.socket, 'load ' + args.model)
                request(args.socket, 'retain ' + args.model)
                retained = True
            broker_load_us = (time.perf_counter_ns() - start) / 1000
            for worker in workers:
                worker.stdin.write('load\n')
                worker.stdin.flush()
            timings = [read_line(worker) for worker in workers]
            if not all(line.startswith('load_or_share_open_us') for line in timings):
                raise RuntimeError('worker load failed')
            time.sleep(1)
            used = memory()
            print(json.dumps(dict(mode=mode, processes=args.processes, baseline_mib=baseline,
                                  resident_mib=used, delta_mib=used-baseline,
                                  broker_load_us=broker_load_us if mode == 'shared' else None,
                                  workers=timings)), flush=True)
            for worker in workers:
                worker.stdin.write('close\n')
                worker.stdin.flush()
                print(read_line(worker), flush=True)
                if worker.wait(timeout=30):
                    raise RuntimeError('worker failed')
        finally:
            for worker in workers:
                if worker.poll() is None:
                    worker.kill()
                worker.wait()
            if retained:
                request(args.socket, 'release_model ' + args.model)
                request(args.socket, 'unload ' + args.model)


if __name__ == '__main__':
    main()
