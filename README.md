# gpumemd

A simulated GPU resource and model-residency broker being built incrementally
in modern C++, with no CUDA or Metal dependency yet.

**Current state: v0.9 implemented.** CMake builds the daemon, the independent
`ResourceManager`, `ModelRegistry`, and `ModelResidencyManager` cores, the
text-command parser, and `gpumemctl`. CTest covers accounting, waiting queues,
priorities, timeouts, model lifecycle and concurrency, LRU eviction, parsing,
CLI behavior, and Unix socket integration.

v0.9 supports optional NVIDIA CUDA allocations when CMake finds a CUDA compiler
and toolkit. macOS continues to use Metal, and systems without a usable
accelerator continue to use the mock backend. It does not parse model formats
or run model kernels.
Model footprints and residency consume logical reservations in the daemon's
configured capacity so residency policy can be exercised without hardware.
The accelerator boundary is represented by `AcceleratorBackend`; `MockBackend`
tracks simulated model loads and unloads, while `MetalBackend` allocates one
shared `MTLBuffer` per resident model and `CUDABackend` allocates one CUDA
device buffer per resident model.

## Build and test

Requirements: CMake 3.20+ and a compiler supporting C++20. The intended v0.1
platforms are macOS and Linux. There are no third-party dependencies to download.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tests are enabled by default; configure with `-DBUILD_TESTING=OFF` to disable
them. The suite covers thread-safe accounting, boundary and invalid operations,
command parsing and size units, help, option validation, multi-client operation,
and clean socket shutdown.

## Run the broker

```sh
./build/gpumemd --memory 16GB --socket /tmp/gpumemd.sock
```

The daemon owns the socket path and removes it on orderly shutdown. Press
Ctrl-C to stop it. Existing socket paths are refused rather than overwritten.

In another terminal, use the client:

```sh
./build/gpumemctl --socket /tmp/gpumemd.sock acquire processA 4GB
./build/gpumemctl --socket /tmp/gpumemd.sock try_acquire processB 2GB
./build/gpumemctl --socket /tmp/gpumemd.sock acquire processC 8GB 10 5000
./build/gpumemctl --socket /tmp/gpumemd.sock status
./build/gpumemctl --socket /tmp/gpumemd.sock release processA
./build/gpumemctl --socket /tmp/gpumemd.sock release processB
./build/gpumemctl --socket /tmp/gpumemd.sock release processC
./build/gpumemctl --socket /tmp/gpumemd.sock register bert 7GB bert-base
./build/gpumemctl --socket /tmp/gpumemd.sock register_file bert /models/bert.bin bert-base
./build/gpumemctl --socket /tmp/gpumemd.sock load bert
./build/gpumemctl --socket /tmp/gpumemd.sock retain bert
./build/gpumemctl --socket /tmp/gpumemd.sock residency
./build/gpumemctl --socket /tmp/gpumemd.sock models
./build/gpumemctl --socket /tmp/gpumemd.sock release_model bert
./build/gpumemctl --socket /tmp/gpumemd.sock unload bert
./build/gpumemctl --socket /tmp/gpumemd.sock unregister bert
```

`gpumemctl` returns a non-zero status for broker or command errors.
`acquire` waits for memory when needed; its optional values are priority and
timeout in milliseconds. `try_acquire` never waits. An `acquire` larger than
the daemon's total capacity is impossible and returns `insufficient_memory`
immediately, even when its timeout would otherwise wait forever. A timeout of
`0` is also an immediate, non-blocking attempt.

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

When a load needs capacity, the broker evicts the least-recently-loaded
resident models first, but only models whose reference count is zero. A model
protected by `retain` cannot be evicted or explicitly unloaded until matching
`release_model` calls return its reference count to zero. If enough capacity
cannot be obtained, the load fails with `insufficient_memory` and preserves
all existing residency and reservations; failed loads never partially evict
models.

Simulated registrations use logical footprints. File-backed registrations use
the actual file size and transfer raw bytes, but no model format is parsed.
Metadata is a single non-empty ASCII token of at most 128 bytes; quoting and
whitespace are not supported.

## Cold/warm benchmark

Builds include `gpumemd-bench`, which measures file-backed cold loads and
already-resident warm loads:

```sh
./build/gpumemd-bench --file /models/bert.bin --iterations 5 --backend auto
```

`auto` prefers CUDA, then Metal, then Mock. Use `--backend mock`, `metal`, or
`cuda` to select one explicitly. Cold samples read and transfer the file after
each unload; warm samples repeat the idempotent load while the model remains
resident.

## Structure and next steps

- `include/gpumemd/`: public core types and the resource/registry APIs.
- `src/core/`: hardware-independent resource, registry, and residency logic.
- `include/gpumemd/accelerator_backend.hpp` and `src/backend/`: accelerator
  backend contract, simulated `MockBackend`, and optional Metal implementation.
- `include/gpumemd/protocol.hpp` and `src/protocol/`: text-command parser.
- `src/daemon/`: daemon entry point and option handling.
- `include/gpumemd/server.hpp` and `src/ipc/`: multi-client Unix socket server.
- `src/client/`: `gpumemctl` command-line client.
- `tests/`: CTest checks for core accounting, parsing, CLI behavior, and IPC.

The v0.9 acceptance demonstration combines concurrent resource clients with
three registered models, explicit loads, refcount protection, LRU eviction,
and complete unload/release/unregister cleanup. Final `status` should report
used `0` and all configured capacity free.

Model-format parsing, model kernels, and multi-GPU support remain future work.
CUDA builds select device 0 only and are enabled only when `nvcc` and the CUDA
toolkit are available at configure time.
