# gpumemd

A GPU resource broker being built incrementally in modern C++.
The first release, v0.1, will provide simulated memory reservations through a
Unix domain socket, with no CUDA or Metal dependency.

**Current state: v0.2 implemented.** CMake builds the daemon, a hardware-
independent `ResourceManager`, the text-command parser, and `gpumemctl`. CTest
covers accounting, waiting queues, priorities, timeouts, parsing, CLI behavior,
and Unix socket integration. No GPU memory is allocated; all accounting is
simulated.

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
```

`gpumemctl` returns a non-zero status for broker or command errors.
`acquire` waits for memory when needed; its optional values are priority and
timeout in milliseconds. `try_acquire` never waits.

## Structure and next steps

- `include/gpumemd/`: public core types and the `ResourceManager` API.
- `src/core/`: hardware-independent resource accounting implementation.
- `include/gpumemd/protocol.hpp` and `src/protocol/`: text-command parser.
- `src/daemon/`: daemon entry point and option handling.
- `include/gpumemd/server.hpp` and `src/ipc/`: multi-client Unix socket server.
- `src/client/`: `gpumemctl` command-line client.
- `tests/`: CTest checks for core accounting, parsing, CLI behavior, and IPC.
- [Design](docs/design.md): planned architecture, classes, protocol, accounting
  rules, test strategy, and future CUDA considerations.
- [Template plan](docs/superpowers/plans/2026-09-11-template.md): starter checklist.
- [AGENTS.md](AGENTS.md): project scope and roadmap.

The v0.1 acceptance demonstration uses two concurrent clients to acquire and
release reservations, then checks that final free capacity equals total capacity.
The v0.2 acceptance demonstration also uses a queued `acquire` that succeeds
after another client releases its reservation.

Scheduling queues, model residency, eviction, hardware backends, and multi-GPU
support remain in their later roadmap versions.
# gpumemd
