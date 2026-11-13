# T4 validation: concurrency fixes and pinned loading experiment

Run supplied by the user on 2026-11-13, exported as
`gpumemd-results-ax4sjvpx`. `SOURCE_REVISION.txt` identifies commit
`2e922a4ea678cfa8167573edf67e8f5249277d03` exactly. Raw files were copied from
that export; only the empty daemon log was omitted and CSV line endings were
normalized to LF. `summary.json` is derived from `load-ab.jsonl`, not a raw log.

Hardware: Tesla T4 (15,360 MiB), driver 580.82.07, CUDA 13.0.88, GNU 13.3,
Release build, architecture 75. See the build/configuration and environment logs.

## Validation

`ctest.txt` reports **23/23 tests passed**, including:

- `server_churn`: thousands of short-lived connections with an idle client held
  open. The regression passed on Linux/T4; CTest did not export its successful
  stdout, so this run does not provide a numerical memory-growth measurement.
- `cuda_backend` and `cuda_ipc`: real CUDA checks, including cross-process
  readback of the patterned payload after the new upload publication logic.
- `cuda_backend_contract` and `cuda_load_contract`: host-side runtime-stub tests
  for deterministic concurrency/failure cases and deferred buffer reuse. These
  two tests remain CPU tests even when executed on a CUDA machine.
- Logical accounting, server/client behavior, and benchmark unload handling.

The standalone `acquire-release.json` also reports PASS, seven acquisition
attempts, three successful acquisitions and releases, balanced byte counters
(1,207,959,552 bytes), and zero final reservations. Its 200 ms timeout was
observed at 203.634 ms.

This run validates execution of the updated notebook's combined sequence on T4.
It is not a claim of exhaustive race freedom, crash recovery, or sustained-load
production readiness.

## Pageable versus pinned pipeline

Five paired trials, alternating mode order, same 512 MiB synthetic file.
Pinned mode uses two 8 MiB host buffers and two nonblocking CUDA streams.
Every trial has `verified: true`: all device bytes were read back and compared
with the input file outside the measured interval.

| Measurement (median) | Pageable | Pinned pipeline |
| --- | ---: | ---: |
| Total load wall time | 538.266 ms | 108.230 ms |
| Host setup | 288.833 ms | 8.517 ms |
| File reads | 129.184 ms | 93.632 ms |
| Sum of GPU copy event intervals | 118.982 ms | 47.398 ms |

**Ratio of median total times: 4.973×; median load duration reduced by 79.89%.**
Pageable total times ranged from 512.315–597.224 ms; pinned totals ranged from
96.992–114.544 ms. All five pinned trials were faster than all five pageable
trials in this session.

The gain includes eliminating allocation/zero-initialization of the full
512 MiB pageable vector: staging capacity during loading is 16 MiB in pinned
mode. It also includes different read behavior, faster copy intervals, and
read/transfer overlap. This experiment does not isolate the contribution of
pinned memory alone, nor measure peak PCIe bandwidth. GPU event intervals and
CPU timings overlap; do not add the phase medians to reconstruct total time.

**This is a standalone experimental loader, not a 4.973× faster broker.**
The broker's actual cold-load median is still 537.094 ms (`cold-warm.txt`), with
mean 571.854 ms. Integrating the pipeline into residency loading is separate work.

## Memory sharing and socket latency

All three repetitions at each worker count retained the earlier memory result:

| Workers | Independent delta | Shared delta | Reduction |
| ---: | ---: | ---: | ---: |
| 1 | 512 MiB | 512 MiB | 0% |
| 2 | 1,024 MiB | 512 MiB | 50% |
| 4 | 2,048 MiB | 512 MiB | 75% |
| 8 | 4,096 MiB | 512 MiB | 87.5% |

These deltas exclude initialized-context baseline. The CSV was checked against
all 12 raw worker logs.

The 10,000 socket requests have p50/p95/p99 of **79.974/120.532/195.409 µs**.
The [earlier run](../2026-11-13-colab-t4/README.md) measured
112.453/165.187/219.519 µs. This is a comparison between sessions, not a controlled
A/B of the server change, so it does not isolate the effect of thread reclamation.
The residency daemon's `metrics.txt` again has zero acquires; its counters are
separate from the dedicated acquire/release validation daemon.

## Reproduce and interpret

Follow [the experiment procedure](../../LOAD_EXPERIMENT.md) using the recorded
revision. Context initialization is excluded; buffer setup and device allocation
are included. Teardown and verification are excluded. Filesystem caches are not
flushed, and readback verification rereads the file between trials. Five pairs
on one T4 session are useful evidence, not a guarantee for other files, GPUs,
chunk sizes, or storage systems. No inference kernels were benchmarked.

Suggested wording: “Built and validated a double-buffered pinned-memory CUDA
loading experiment on NVIDIA T4, reducing median load time for 512 MiB from
538 ms to 108 ms (4.97×) over five paired trials with full readback verification.”
