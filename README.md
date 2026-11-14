# gpumemd

**Share one GPU-resident copy of your data across multiple processes.**

gpumemd is a C++20 memory broker with a local daemon, CLI, and C++ client.
It manages raw model-file residency and lets NVIDIA GPU workers import the
same allocation through CUDA IPC.

**Validated on NVIDIA T4:** eight workers shared a single 512 MiB allocation,
using **87.5% less incremental VRAM** than eight independent copies across
three trials.

[Quickstart](docs/QUICKSTART.md) ·
[Try on Colab](https://colab.research.google.com/github/yahiag04/gpumemd/blob/v0.18.1/notebooks/gpumemd_t4.ipynb) ·
[C++ integration](docs/CPP_INTEGRATION.md) ·
[Benchmark report](docs/benchmarks/2026-11-13-colab-t4-load-ab/README.md) ·
[Download v0.18.1](https://github.com/yahiag04/gpumemd/releases/tag/v0.18.1)

**Experimental · MIT licensed · Source distribution.**
The broker manages memory; it does not parse model formats or run inference.

## Why use it?

Multiple GPU worker processes may need the same large dataset or model weights.
Keeping a separate copy in every process consumes VRAM that could otherwise
hold inputs, outputs, or additional data.

With gpumemd, the daemon loads the raw bytes once and workers import that
allocation. A coordinator keeps the data resident until all consumers finish.
For example, custom CUDA workers could share a read-only vector database while
each processes its own queries.

This is a building block for developers integrating their own CUDA workloads.
Applications must use the client API; existing frameworks do not automatically
start sharing memory when the daemon is launched.

## Results on NVIDIA T4

Measured in Google Colab with a **512 MiB synthetic file**, CUDA 13.0, a Release
build, and three trials per worker count. All three trials produced the same
memory deltas at each count.

| Workers | Independent VRAM delta | Shared VRAM delta | Reduction |
| ---: | ---: | ---: | ---: |
| 1 | 512 MiB | 512 MiB | 0% |
| 2 | 1,024 MiB | 512 MiB | 50% |
| 4 | 2,048 MiB | 512 MiB | 75% |
| 8 | 4,096 MiB | 512 MiB | **87.5%** |

These deltas exclude the initialized CUDA-context baseline. At eight workers,
**total observed GPU usage** fell from 5,017 to 1,433 MiB: a **71.4% reduction**.

Socket round-trip latency over 10,000 requests:

| p50 | p95 | p99 |
| ---: | ---: | ---: |
| 80 µs | 121 µs | 195 µs |

Socket timings include connection setup and Python overhead. These measurements
show memory sharing and broker latency, not inference throughput. See the
[latest report and raw logs](docs/benchmarks/2026-11-13-colab-t4-load-ab/README.md)
for loading times, methodology, environment, and limitations.

A **standalone pinned-memory loading experiment** reduced median loading time
for 512 MiB from **538 ms to 108 ms (4.97×)** over five paired trials, with full
readback verification. This pipeline is not yet integrated into the broker's
loading path; it is not a 4.97× broker or inference speedup.

## Get started

### On your machine

For CUDA sharing, follow the [Linux/NVIDIA quickstart](docs/QUICKSTART.md).
It covers compilation, a two-process IPC demo, concurrent memory measurements,
and cleanup.

For a general local build on Linux or macOS:

```sh
git clone --branch v0.18.1 --depth 1 https://github.com/yahiag04/gpumemd.git
cd gpumemd
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Requirements: **CMake 3.20+ and a C++20 compiler**. CUDA additionally requires
`nvcc` and a compatible toolkit/driver. Python 3 enables the benchmark scripts
and additional CTest checks. The release contains sources, not prebuilt binaries.

| Backend | Requirements | Residency | Cross-process sharing |
| --- | --- | --- | --- |
| CUDA | Linux, NVIDIA GPU and CUDA toolkit | Device allocations and raw-file transfer | CUDA IPC |
| Metal | macOS and system Metal framework | Shared-buffer allocations and raw-file transfer | Not implemented |
| Mock | Linux or macOS; no GPU SDK required | Simulated | Not supported |

CUDA builds are enabled when CMake finds the compiler and toolkit. A daemon
without a usable accelerator can fall back to mock; a successful mock run does
not validate GPU sharing.

### Google Colab / T4

1. [Open the notebook](https://colab.research.google.com/github/yahiag04/gpumemd/blob/v0.18.1/notebooks/gpumemd_t4.ipynb) and select a T4 GPU runtime.
2. Download `gpumemd-colab-source.zip` from the [release](https://github.com/yahiag04/gpumemd/releases/tag/v0.18.1).
3. Run the cells in order and upload that ZIP when requested.
4. Download the results ZIP before disconnecting.

The notebook builds the project, runs tests and the IPC demo, measures
cold/resident loads, socket latency, and independent/shared VRAM, then checks
logical acquire/release behavior. Use the named release asset: its files are
at the archive root, unlike GitHub's automatically generated source ZIP.

To package a different committed checkout for the notebook:

```sh
git archive --format=zip --output=gpumemd-colab-source.zip HEAD
```

## How it works

The daemon owns resident allocations. Clients communicate through a Unix
socket, and CUDA consumers import device pointers through the C++ wrapper.
There are three separate operations:

| Purpose | Commands / API | Effect |
| --- | --- | --- |
| Reserve logical capacity | `acquire`, `try_acquire`, `release` | Accounting and waiting; no CUDA allocation |
| Manage resident data | `register_file`, `load`, `unload` | Register a raw file and move its bytes into/out of the backend |
| Protect and share data | `retain`, `release_model`, `share` / `open_model()` | Prevent eviction and import a CUDA allocation |

**Keep the allocation alive while consumers use it.** The coordinator must
retain the model before loading and release it after every consumer finishes
GPU work and closes its imported mapping. `open_model()` does not retain the
model automatically; its RAII wrapper closes only the local IPC mapping.

The [C++ integration guide](docs/CPP_INTEGRATION.md) provides a complete example,
CMake linking instructions, and the ownership rules.

## Features and scope

- **Residency management:** file-backed loading, reference protection, and
  eviction informed by recency, load cost, load history, and footprint.
- **Resource scheduling:** logical capacity accounting, priority/FIFO queues,
  optional cost-aware scheduling, non-blocking requests, and timeouts.
- **Local clients:** Unix-socket daemon, command-line client, C++ client library,
  and CUDA IPC mapping wrapper.
- **Observability:** status, residency, registry, and request/byte counters.

Supporting components include a pool allocator, device-placement accounting,
and a remote-node registry. Placement is not multi-GPU routing, and the node
registry does not implement distributed transport or replication.

Use trusted processes on one host and one visible GPU. This experimental
release has no authentication, automatic client-crash reference recovery,
persistent service recovery, or stable API/ABI guarantee. Concurrent writes
and GPU synchronization are the application's responsibility. Model-format
parsing and inference execution are outside the project's scope.

## Validation

The latest T4 run at commit `98e44c9` passed **23/23 CTest checks**, including
connection churn, CUDA backend checks, and cross-process payload readback.
The combined notebook sequence also completed, including acquire/release
validation and five paired pageable/pinned loading trials with verified data.

Two CUDA contract tests use a stub runtime to exercise deterministic host-side
concurrency and failure cases; the real GPU tests are separate. These results
do not establish exhaustive race freedom or production readiness. See the
[latest validation report](docs/benchmarks/2026-11-13-colab-t4-load-ab/README.md).

Run the standalone logical accounting check with:

```sh
python3 scripts/check_acquire_release.py --build build
```

### Changes after v0.18.1

`main` fixes retention of completed server threads and premature publication of
CUDA backend uploads. It also includes an experimental pageable/pinned loading
comparison. These changes have local regression coverage and have now been
compiled and exercised on the T4 at commit `98e44c9`. The published v0.18.1
release and historical T4 measurements predate these fixes. See the
[fixes, test evidence, and A/B procedure](docs/LOAD_EXPERIMENT.md).

## Documentation and source

| Resource | Contents |
| --- | --- |
| [Quickstart](docs/QUICKSTART.md) | First CUDA run, demo, measurements, troubleshooting |
| [C++ integration](docs/CPP_INTEGRATION.md) | Linking, device pointers, retention and cleanup |
| [Command and benchmark reference](docs/REFERENCE.md) | Protocol semantics, scheduling, benchmark commands |
| [Latest T4 report](docs/benchmarks/2026-11-13-colab-t4-load-ab/README.md) | 23-test validation, pinned loading A/B, and raw evidence |
| [Initial T4 report](docs/benchmarks/2026-11-13-colab-t4/README.md) | Historical release measurements and methodology |
| [Loading experiment](docs/LOAD_EXPERIMENT.md) | Post-release concurrency fixes and pageable/pinned A/B procedure |
| [Release notes](docs/releases/v0.18.1.md) | Packaged features and experimental release scope |

Public APIs live in `include/gpumemd/`; implementations are under `src/core/`,
`src/backend/`, `src/ipc/`, and `src/client/`. Tests are in `tests/`, and
reproducible experiments are in `scripts/` and `notebooks/`.

For reproducible bugs, open a [GitHub issue](https://github.com/yahiag04/gpumemd/issues)
with the revision, GPU, compiler/toolkit versions, command, and relevant output.

## License

[MIT](LICENSE) — Copyright (c) 2026 Yahia Ghallale.
