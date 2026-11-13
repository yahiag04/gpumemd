# Loading experiment and concurrency fixes after v0.18.1

The historical T4 results remain unchanged. The changes described here are on
`main`, after the v0.18.1 tag. A subsequent run at commit `2e922a4` compiled and
exercised the changes on NVIDIA T4: **23/23 CTest checks passed**, and the
standalone pinned pipeline reduced median 512 MiB load time from **538.266 ms
to 108.230 ms (4.973×)** over five paired trials with full readback verification.
See the [new report and raw evidence](benchmarks/2026-11-13-colab-t4-load-ab/README.md).
The experiment is still separate from the broker's loading path.

## Confirmed defects

**Completed server sessions were retained until shutdown.** Each short-lived
connection left a joinable thread in the server's vector. A local macOS
regression reproduced 262,144 KiB of additional virtual memory after 8,192
connections, following warmup. The fixed run showed 0 KiB growth in the same
experiment. These are process virtual-memory measurements, not RSS or VRAM.

The server now reaps completed asynchronous sessions during normal operation,
including idle polling. It still uses one thread per active connection; this
fix does not bound simultaneous clients or remove thread-creation overhead.
A bounded worker pool cannot simply wrap the current blocking dispatch:
waiting acquires can occupy every worker and starve the releases needed to
wake them. Bounded concurrency requires nonblocking scheduling or another
explicit policy. The churn regression also keeps an idle connection open.

**The CUDA backend published an allocation before its data was ready.**
`load(span)` previously called the allocation-only overload, unlocked, then
copied under the registry mutex. Direct backend callers could observe the
allocation in that gap, or wait behind the full copy. The daemon's existing
`ModelResidencyManager` mutex serializes load/share and masks that direct-backend
race; it still serializes residency operations during file loading.

The backend now reserves the id privately, allocates and copies outside the
registry mutex, synchronizes the copy's stream, and then publishes the pointer.
While loading, share/unload return `NotLoaded`, snapshots omit the allocation,
and another load of the same id returns `AlreadyLoaded`. Failure removes the
reservation and frees private storage. A host-side test gates the runtime copy
to exercise visibility, unrelated operations, and allocation/copy/sync failures.
It uses a stub CUDA runtime and does not prove hardware correctness. The real
CUDA IPC test now reads back patterned bytes in the separate consumer process.

## Why a separate A/B benchmark?

The recorded ~530–620 ms for 512 MiB includes host allocation/initialization,
file I/O, device allocation, and transfer. It is an end-to-end loading rate,
not isolated PCIe bandwidth. Pinned staging and asynchronous copies can overlap
file reads with transfers, but improvement depends on the environment and must
be measured. See NVIDIA's
[asynchronous transfer guidance](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/#asynchronous-and-overlapping-transfers-with-computation).

`gpumemd-cuda-load-bench` compares two standalone implementations:

- `pageable`: allocate/initialize a full host vector, read the file, then one
  synchronous `cudaMemcpy` plus completion synchronization.
- `pinned_pipeline`: two pinned host buffers, two nonblocking streams, and
  chunked `cudaMemcpyAsync`. A buffer is reused only after its prior copy finishes.

The broker's production file-loading path is not switched to this experiment.
This avoids claiming a faster broker before a measured and integrated change.
The CPU contract test defers fake DMA until synchronization to catch premature
buffer reuse, partial final chunks, corruption, and pinned-allocation failure.
Its timing output is synthetic and must not be used as performance evidence.

## Run on the T4

Use a fresh checkout/build of `main` (the v0.18.1 source asset predates these
changes), and keep the GPU otherwise idle. From the checkout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=75
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/gpumemd-cuda-load-bench --file /absolute/model.bin --iterations 5 --chunk-mib 8
```

The updated notebook runs the same command when the new target is present and
exports `load-ab.jsonl`. To use it in Colab, generate a flat ZIP of the updated
checkout with `git archive --format=zip --output=gpumemd-colab-source.zip HEAD`.
Use a fresh runtime or remove the previous source/build directory before upload;
the notebook otherwise keeps its existing checkout. Keep the GPU/toolkit details,
commit hash, file size, chunk size, and raw per-trial JSON with the results.

Each mode runs once per trial, alternating order between trials. Context
initialization is excluded, buffer/stream/event setup and device allocation
are included, and teardown/readback verification are excluded from `total_ms`.
Every completed trial verifies the entire device allocation against the file.

| Field | Meaning |
| --- | --- |
| `total_ms` | Wall time from opening the file through completed GPU transfer |
| `host_setup_ms` | Host buffers and stream/event setup; includes pageable vector initialization |
| `device_alloc_ms` | Wall time in device allocation |
| `file_read_ms` | Sum of wall time in reads into host buffers |
| `h2d_gpu_ms` | Sum of CUDA-event intervals around copies; includes scheduling/staging effects, not pure PCIe wire time |
| `submit_wall_ms` | Wall time submitting events/copies; pageable copy can block here |
| `wait_ms` | CPU time waiting for recorded copy completion |
| `verified` | Full readback matched the file, outside the measured interval |

GPU intervals and CPU read/submit/wait times can overlap: do not add all fields
to estimate total duration. Filesystem caches are not flushed, and verification
rereads the file. Compare paired trials and medians, not just the best sample.
Report speedup as `median(pageable total_ms) / median(pinned_pipeline total_ms)`.
There is no promised improvement, and neither result measures inference.
