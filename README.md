# gpumemd

A GPU resource and model-registry broker being built incrementally in modern
C++, with no CUDA or Metal dependency yet.

**Current state: v0.3 implemented.** CMake builds the daemon, the independent
`ResourceManager` and `ModelRegistry` cores, the text-command parser, and
`gpumemctl`. CTest covers accounting, waiting queues, priorities, timeouts,
model lifecycle and concurrency, parsing, CLI behavior, and Unix socket
integration. No GPU memory is allocated; resource accounting is simulated and
the registry records model metadata only.

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
./build/gpumemctl --socket /tmp/gpumemd.sock register bert 7GB bert-base
./build/gpumemctl --socket /tmp/gpumemd.sock retain bert
./build/gpumemctl --socket /tmp/gpumemd.sock models
./build/gpumemctl --socket /tmp/gpumemd.sock release_model bert
./build/gpumemctl --socket /tmp/gpumemd.sock unregister bert
```

`gpumemctl` returns a non-zero status for broker or command errors.
`acquire` waits for memory when needed; its optional values are priority and
timeout in milliseconds. `try_acquire` never waits. An `acquire` larger than
the daemon's total capacity is impossible and returns `insufficient_memory`
immediately, even when its timeout would otherwise wait forever. A timeout of
`0` is also an immediate, non-blocking attempt.

## Model registry

`register NAME SIZE METADATA` creates a model record with reference count and
last-access sequence set to zero. `retain NAME` increments its reference count,
and `release_model NAME` decrements a positive count; reaching zero leaves the
record registered. `unregister NAME` removes only a zero-reference record and
returns `model_in_use` otherwise. `models` lists records sorted by name with
their metadata, declared footprint in bytes, reference count, and monotonic
last-access sequence.

The registered footprint is **descriptive only** in v0.3. Registering or
retaining a model does not reserve simulated GPU memory, change `status`, load
model data, or establish residency. Metadata is a single non-empty ASCII token
of at most 128 bytes; quoting and whitespace are not supported.

## Structure and next steps

- `include/gpumemd/`: public core types and the resource/registry APIs.
- `src/core/`: hardware-independent resource accounting and model registry.
- `include/gpumemd/protocol.hpp` and `src/protocol/`: text-command parser.
- `src/daemon/`: daemon entry point and option handling.
- `include/gpumemd/server.hpp` and `src/ipc/`: multi-client Unix socket server.
- `src/client/`: `gpumemctl` command-line client.
- `tests/`: CTest checks for core accounting, parsing, CLI behavior, and IPC.
- [Design](docs/design.md): planned architecture, classes, protocol, accounting
  rules, test strategy, and future CUDA considerations.
- [Template plan](docs/superpowers/plans/2026-09-11-template.md): starter checklist.
- [AGENTS.md](AGENTS.md): project scope and roadmap.

The v0.3 acceptance demonstration combines concurrent resource clients with a
model registration/retain/release lifecycle, then verifies that resource
capacity is fully restored and the unregistered model no longer appears.

Model residency, loading, eviction, accelerator backends, GPU allocation,
CUDA/Metal integration, and multi-GPU support remain future work. The v0.3
registry deliberately does not implement those subsystems.
