# gpumemd

A GPU resource broker being built incrementally in modern C++.
The first release, v0.1, will provide simulated memory reservations through a
Unix domain socket, with no CUDA or Metal dependency.

**Current state: repository template.** CMake builds a CLI stub and CTest checks
its startup behavior. Resource accounting, socket transport, and a client are
not implemented yet. No GPU memory is allocated.

## Build and test

Requirements: CMake 3.20+ and a compiler supporting C++20. The intended v0.1
platforms are macOS and Linux. There are no third-party dependencies to download.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tests are enabled by default; configure with `-DBUILD_TESTING=OFF` to disable
them. The current three checks cover help, missing arguments, and unsupported
startup arguments. They do not test resource accounting or IPC.

## Run the template

```sh
./build/gpumemd --help
```

Running without arguments exits with status 1 and explains that broker startup
is not implemented. Other arguments exit with status 2.

## Structure and next steps

- `include/gpumemd/`: public core types, starting with an unsigned 64-bit byte type.
- `src/daemon/`: executable entry point.
- `tests/`: CTest checks; core unit and IPC integration tests will be added here.
- [Design](docs/design.md): planned architecture, classes, protocol, accounting
  rules, test strategy, and future CUDA considerations.
- [Template plan](docs/superpowers/plans/2026-09-11-template.md): starter checklist.
- [AGENTS.md](AGENTS.md): project scope and roadmap.

The next increment is a thread-safe `ResourceManager` with unit tests. Protocol
parsing and Unix socket transport follow. Once v0.1 is implemented, acceptance
includes a live demonstration with at least two concurrent clients and a final
status showing that released capacity is restored.

Scheduling queues, model residency, eviction, hardware backends, and multi-GPU
support remain in their later roadmap versions.
# gpumemd
