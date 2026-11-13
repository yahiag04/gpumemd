# Command and benchmark reference

For installation and a complete first run, start with the
[quickstart](QUICKSTART.md). For imported CUDA pointers and reference ownership,
see the [C++ integration guide](CPP_INTEGRATION.md).

## Logical reservations

`acquire NAME SIZE [PRIORITY TIMEOUT_MS COST_MS]` reserves logical capacity,
waiting if necessary. `try_acquire NAME SIZE` never waits. `release NAME`
returns a successful reservation to the broker. These operations do not
allocate CUDA memory or return a device pointer.

An acquire larger than total capacity returns `insufficient_memory`
immediately. Timeout `0` is a non-blocking attempt. Duplicate active client
names are rejected. `gpumemctl` exits nonzero for broker or command errors.
Use `status` to inspect capacity, usage, and reservations, and `metrics` for
request and byte counters.

Scheduling defaults to priority/FIFO. Start the daemon with `--scheduling cost`
to use estimated load cost as well; the sixth command token is `COST_MS`:

```sh
./build/gpumemctl --socket /tmp/gpumemd.sock acquire worker 256MB 10 5000 100
./build/gpumemctl --socket /tmp/gpumemd.sock release worker
```

## Model registry and residency


`register NAME SIZE METADATA` creates a model record with reference count and
last-access sequence set to zero. `retain NAME` increments its reference count,
and `release_model NAME` decrements a positive count; reaching zero leaves the
record registered. `unregister NAME` removes only a zero-reference record and
returns `model_in_use` otherwise. `models` lists records sorted by name with
their metadata, declared footprint in bytes, reference count, and monotonic
last-access sequence.

`register_file NAME PATH METADATA` registers a raw binary model file. Its
footprint is read from the file size, and `load NAME` reads the file and copies
its bytes into the selected accelerator backend. Paths and metadata are single
tokens; quoting and whitespace in paths are not supported by the text protocol.
`models` includes the source path for file-backed records.

`share NAME` exports a resident CUDA allocation as a hexadecimal CUDA IPC token:

```sh
./build/gpumemctl --socket /tmp/gpumemd.sock share bert
```

The token is valid only while the daemon keeps the model resident. It can be
opened from another CUDA process through `CudaIpcClient`, then closed in that
process. Mock and Metal backends report `unsupported`; CUDA IPC currently uses
device 0 and does not provide persistence, authentication, networking, or
multi-GPU routing.

`load NAME` explicitly makes a registered model resident and reserves its
declared footprint under `model:NAME`; `unload NAME` explicitly releases that
reservation. Loading an already resident model is idempotent. `residency`
lists resident models with footprint, reference count, and monotonic load
order. Registration and retention alone do not make a model resident.

When a load needs capacity, the broker ranks resident models using recency,
measured load cost, load history, and footprint, and evicts only models whose
reference count is zero. A model
protected by `retain` cannot be evicted or explicitly unloaded until matching
`release_model` calls return its reference count to zero. If enough capacity
cannot be obtained, the load fails with `insufficient_memory` and preserves
all existing residency and reservations; failed loads never partially evict
models.

Simulated registrations use logical footprints. File-backed registrations use
the actual file size and transfer raw bytes, but no model format is parsed.
Metadata is a single non-empty ASCII token of at most 128 bytes; quoting and
whitespace are not supported.

## Cold and resident load benchmarks


Builds include `gpumemd-bench`, which measures file-backed cold loads and
already-resident warm loads:

```sh
./build/gpumemd-bench --file /models/bert.bin --iterations 5 --backend auto
```

`auto` prefers CUDA, then Metal, then Mock. Use `--backend mock`, `metal`, or
`cuda` to select one explicitly. Cold samples read and transfer the file after
each unload; warm samples repeat the idempotent load while the model remains
resident.

## Socket latency and multiprocess VRAM


Build in Release mode. `gpumemd-bench` now reports nearest-rank p50/p95/p99
in nanoseconds for cold loads and at least 10,000 resident load calls, after
1,000 warm-up calls. These are in-process idempotent loads, not inference.
Clock overhead is included; filesystem caches are not flushed.

For socket round trips (including connection establishment and Python overhead),
register a disposable model in a running daemon, then run:

```sh
python3 scripts/benchmark.py --socket /tmp/gpumemd.sock --model bench
```

On a dedicated idle NVIDIA GPU, build the CUDA targets, start the daemon and
register a disposable file-backed model named `bench`. Use the same file for
independent workers and the broker:

```sh
./build/gpumemctl --socket /tmp/gpumemd.sock register_file bench /absolute/model.bin benchmark
python3 scripts/benchmark.py --socket /tmp/gpumemd.sock --model bench \
  --cuda-worker ./build/gpumemd-cuda-bench-worker --file /absolute/model.bin --processes 4
./build/gpumemd-bench --file /absolute/model.bin --backend cuda --iterations 20
```

The comparison unloads this model and runs independent then shared workers.
All N CUDA contexts initialize before the baseline. Workers stay alive until
the GPU-wide resident measurement; shared mode includes the daemon allocation.
`nvidia-smi` samples GPU 0 memory in MiB, avoiding double counting IPC mappings.
Run with CUDA device visibility matching physical GPU 0 and no other workloads.
Repeat for N=1,2,4,8 and several trials; compare `delta_mib` and report
`1 - shared_delta / independent_delta`. Expected payload alone is N*S versus S;
actual deltas include allocation granularity and IPC overhead. Negative/noisy
deltas or insufficient VRAM invalidate a trial. Do not sum per-process RSS.

Worker timings separate file read/allocation/copy or broker share+IPC open,
explicit device synchronization, and close/free. Shared workers never mutate
model data. Synchronization measures an idle-device barrier after synchronous
loading, not cross-process producer/consumer synchronization. CUDA experiments
require NVIDIA hardware and are not validated by a successful Metal run.

## Supporting components

- `MemoryAllocator`: pool allocation, block reuse, and coalescing, alongside
  direct resource accounting.
- `DevicePlacement`: least-used-device selection and model assignment accounting;
  it does not route allocations across multiple GPUs.
- `NodeRegistry`: endpoint, capacity, usage, and health records exposed by
  `node_register`, `node_remove`, and `nodes`; it does not implement transport
  or replication.

The daemon removes its socket on orderly shutdown and refuses existing socket
paths. CUDA uses device 0 in the process's visible device set. Metal allocates
one shared `MTLBuffer` per resident model, but cross-process Metal sharing is
not implemented. Mock simulates residency without accelerator hardware.
