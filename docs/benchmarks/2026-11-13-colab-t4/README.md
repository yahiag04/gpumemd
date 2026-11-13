# NVIDIA T4 validation — 2026-11-13

This run validates the v0.18 prototype's CUDA memory sharing and, in a separate
process, logical acquire/release behavior. It does not measure inference.

## Evidence and provenance

The retained raw files in this directory were copied unchanged from the user's Colab
export `gpumemd-results-9aifd8qb`. CSV line endings were normalized to LF without changing values.
`acquire-release.json` was
transcribed from the successful output the user supplied from the same T4
runtime after that export. It is not a locally generated result. Build/configuration logs and the empty
daemon log were omitted; test results and measurement logs are retained.

`SOURCE_REVISION.txt` records base commit
`5a1a73876391097b7bc02ab405130a7e5fc625a6`, not an exact final source snapshot.
Two patches were applied in Colab before the successful run:

- Keep C++/CUDA test assertions enabled in Release with `-UNDEBUG`.
- Ignore only `ERR unknown_residency` when preparing an already unloaded
  benchmark model. Other errors remain fatal.

Both fixes are included in the repository. The updated notebook also runs the
separate acquire/release check; this combined notebook sequence has not itself
been rerun end to end on T4. The individual runs below have been observed.

## Environment and procedure

- Google Colab, Tesla T4, 15,360 MiB VRAM; driver 580.82.07.
- CUDA toolkit 13.0.88; GNU C++ 13.3.0 reported during initial configuration.
- Release build, CUDA architecture 75.
- 512 MiB synthetic binary file written to local runtime storage.
- Independent then shared workers; N=1,2,4,8, three repetitions each.
- GPU-wide `nvidia-smi` memory sampling after all worker contexts initialize.
- Twenty cold loads, 10,000 resident loads after 1,000 warmups.
- 10,000 socket round trips after 1,000 warmups.

Reproduce using [the notebook](../../../notebooks/gpumemd_t4.ipynb) and the
[repository instructions](../../../README.md#google-colab--t4). Save the results
ZIP before disconnecting. Python is needed for the two additional CTest checks;
the C++ tests remain available without it.

## Memory measurements

All three trials yielded the same memory values for each process count.

| Workers | Initialized baseline (MiB) | Independent delta (MiB) | Shared delta (MiB) | Delta reduction |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 207 | 512 | 512 | 0% |
| 2 | 309 | 1,024 | 512 | 50% |
| 4 | 513 | 2,048 | 512 | 75% |
| 8 | 921 | 4,096 | 512 | 87.5% |

Reduction is `1 - shared_delta / independent_delta`. At eight workers,
absolute GPU usage was 5,017 versus 1,433 MiB, a 71.4% reduction including
baseline. Context overhead grows with the worker count and is not eliminated.
See `memory.csv` and `memory-n*-trial*.txt` for the evidence.

## Latency measurements

| Operation | p50 | p95 | p99 |
| --- | ---: | ---: | ---: |
| Cold file-backed CUDA load | 533.768 ms | 642.953 ms | 701.613 ms |
| Already-resident in-process load | 125 ns | 128 ns | 201 ns |
| Socket load round trip | 112.453 microseconds | 165.187 microseconds | 219.519 microseconds |

Mean cold load was 560.467 ms. The printed `warm_avg_ms 0.000` is rounding,
not zero latency; use the nanosecond percentiles. Warm loads are idempotent
residency lookups, not file transfers or inference.

At eight workers, independent load times ranged from 2.797 to 3.199 seconds
per concurrently running worker. Shared mode first loaded the broker allocation
in 615.639–633.277 ms, then workers opened IPC mappings in 0.726–4.042 ms.
Opening IPC excludes that initial broker load. These component timings are not
an end-to-end application startup benchmark; concurrent times must not be summed.

## Correctness checks

- `ctest.txt`: 18/18 tests passed after restoring Release assertions, including
  CUDA backend, CUDA IPC, server, and client tests. The current suite adds two
  Python checks, so its test count differs from this historical run.
- `ipc.txt`: separate process opened the allocation, executed `cudaMemset`, and
  completed the demo. The synthetic contents are disposable.
- `acquire-release.json`: PASS; seven acquire attempts, three successful
  acquisitions and three releases, four expected failures, 1,207,959,552 bytes
  acquired and released. Requested timeout 200 ms, observed 202.043 ms. A waiting
  client completed after release; final logical usage and reservations were zero.
- The user-provided multiprocess output also ended at zero logical usage and
  reservations. That final status was printed, not included in the original ZIP.

`metrics.txt` belongs only to the residency benchmark daemon; its `acquires 0`
and `releases 0` are expected. The separate validation daemon supplies the
acquire/release counters. These commands reserve logical capacity; they do not
perform CUDA allocations. `metrics.txt` also contains 23 failed requests; these
include deliberately tolerated unloads of nonresident models. No per-request
trace was exported, so those aggregate failures cannot all be individually
attributed from these artifacts.

## Limits of the conclusions

This is one T4 session with synthetic bytes and three memory repetitions, not
an inference engine benchmark or a production readiness assessment. Filesystem
caches are not flushed: cold means not resident in GPU. Fixed independent/shared
ordering can bias timing through caches and clock state. Cold p99 uses only
20 samples and is effectively the maximum. Socket times include connection
setup and Python overhead. Synchronization measures an idle-device barrier,
not producer/consumer correctness. No crash recovery, long-running stress,
multi-GPU routing, or isolation/security guarantees are established by this run.

## Suggested CV wording

Built a C++20 GPU memory broker with CUDA IPC, validated on NVIDIA T4;
sharing a 512 MiB allocation across eight worker processes reduced incremental
VRAM by 87.5% versus independent copies across three trials. Validated logical
resource accounting, waiting requests, timeouts, and release behavior.
