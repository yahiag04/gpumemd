# Integrating the C++ client

The broker is a local daemon. Your application sends commands through a Unix
socket and imports CUDA allocations using the CUDA client library. It does not
parse tensors, launch your model, or automatically replace framework allocations.
The API is experimental and is not promised stable across releases.

## Build the existing integration example

[The CUDA demo](../src/examples/cuda_ipc_demo.cu) is a complete client already
built by this repository as `gpumemd-cuda-demo`. It was exercised on the T4.
Start with [the quickstart](QUICKSTART.md), which includes coordinator-side
retention and two separate demo processes.

To link the same client into a separate CMake project, place a checkout of this
release under `vendor/gpumemd/`. For example, use the tested demo source as the
initial `main.cu`:

```sh
mkdir -p vendor
git clone --branch v0.18.1 --depth 1 https://github.com/yahiag04/gpumemd.git vendor/gpumemd
cp vendor/gpumemd/src/examples/cuda_ipc_demo.cu main.cu
```

Create this `CMakeLists.txt` next to `main.cu`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_gpu_client LANGUAGES CXX CUDA)
set(BUILD_TESTING OFF CACHE BOOL "Build vendor tests")
add_subdirectory(vendor/gpumemd EXCLUDE_FROM_ALL)
if(NOT TARGET gpumemd_cuda)
    message(FATAL_ERROR "gpumemd CUDA support is required")
endif()
add_executable(my_gpu_client main.cu)
set_target_properties(my_gpu_client PROPERTIES
    CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED YES)
target_link_libraries(my_gpu_client PRIVATE gpumemd_cuda)
```

Build on the T4 with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=75
cmake --build build --target my_gpu_client --parallel 2
./build/my_gpu_client --socket /tmp/gpumemd-quickstart.sock --model demo
```

The broker must be running with `demo` registered, retained, and loaded, as in
the quickstart. The copied demo mutates the allocation with `cudaMemset`; replace
that operation with your own workload before using real data. The external
CMake layout is a documented integration recipe, not a separately measured T4 run.

## The pointer and its lifetime

The core operations in the demo are:

```cpp
gpumemd::CudaClient client(socket_path);
auto shared = client.open_model(model);
if (!shared.ok()) {
    // Handle the broker rejection or CUDA IPC import failure.
    return 1;
}
void* device_pointer = shared.allocation->pointer();
auto byte_count = shared.allocation->bytes();
// Pass device_pointer to your CUDA operations; do not dereference it on the CPU.
// Finish all asynchronous GPU work before destroying shared.allocation.
```

`CudaIpcAllocation` closes the imported mapping when destroyed. Do not call
`cudaFree` on this pointer: the allocation belongs to the daemon. Closing the
mapping does not release a broker reference or unload the allocation.

A coordinator must keep the daemon and allocation alive for every importing
process. Use this order:

1. Register the file, then `retain NAME` before `load NAME`.
2. Start consumers; each calls `open_model(NAME)` and checks the result.
3. Wait for GPU work to finish, then close every imported mapping.
4. After all consumers finish, `release_model NAME`, then `unload NAME`.

The coordinator can use `gpumemd::Client::request("retain NAME")` and
`request("release_model NAME")`; check each response with `.ok()`.
Retaining before loading avoids an eviction window between those operations.
On an ordinary load failure, balance the successful retain with a release.
A crashed coordinator requires explicit recovery; this release has no automatic
lease expiry or client-death cleanup. Do not stop the daemon while consumers
still use its allocations.

## Logical capacity versus model residency

For non-CUDA applications, link `gpumemd_client` and include
`gpumemd/client.hpp`. Its `acquire`, `try_acquire`, and `release` methods account
for logical capacity; they do not return a GPU allocation. Match every successful
acquire with a release. Duplicate live client names are rejected.

`load`/`unload` control model residency. `retain`/`release_model` protect that
residency. `share`/`open_model` export/import the CUDA allocation. These operations
are separate; none implies all the others.

## Scope

Use trusted clients on the same host and the same visible CUDA device, with
consistent device visibility. IPC is not a network protocol. Concurrent writers
need synchronization designed by the application; the broker does not provide
cross-process execution dependencies. Keep data read-only where practical.
Names, paths, and metadata are single protocol tokens, without whitespace.
See [recorded validation and limitations](benchmarks/2026-11-13-colab-t4/README.md).
