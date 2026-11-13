# First run on a Linux NVIDIA GPU

This guide targets developers trying the experimental v0.18.1 release.
Use one local GPU and trusted processes running under the same user.
For a T4 without local hardware, use the [Colab notebook](https://colab.research.google.com/github/yahiag04/gpumemd/blob/v0.18.1/notebooks/gpumemd_t4.ipynb)
and upload the `gpumemd-colab-source.zip` asset from the
[release](https://github.com/yahiag04/gpumemd/releases/tag/v0.18.1) when prompted.
The notebook builds, checks CUDA IPC, runs the memory benchmarks, and exports logs.

## 1. Build

Requirements: Linux, Git, CMake 3.20+, a C++20 compiler, Python 3, an NVIDIA
GPU/driver, and a CUDA toolkit with C++20 support (`nvcc` must be on PATH).
The recorded T4 environment used GNU 13.3, CUDA 13.0.88, and driver 580.82.07.
Other CUDA/compiler combinations have not been validated by this release.
Compilation time depends on the machine.

```sh
git clone --branch v0.18.1 --depth 1 https://github.com/yahiag04/gpumemd.git
cd gpumemd
nvidia-smi
nvcc --version
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_COMPILER="$(command -v nvcc)" \
  -DCMAKE_CUDA_ARCHITECTURES=75
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Architecture `75` is for the T4. Set the appropriate architecture for your GPU.
The CUDA demo and worker must exist under `build/`; a mock-only build does not
validate GPU sharing. Metal and mock builds remain available for other local
experiments, but this guide's sharing steps require CUDA.

## 2. Start the broker — terminal A

From the repository directory:

```sh
export CUDA_VISIBLE_DEVICES=0
./build/gpumemd --memory 1GB --socket /tmp/gpumemd-quickstart.sock
```

Keep this terminal running. `--memory` sets logical capacity, not a GPU-wide
limit enforced on other applications. The broker uses device 0 in its visible
device set. The benchmark samples physical GPU 0, so keep the mapping above
and use an otherwise idle GPU.

## 3. Load and share — terminal B

Open a second terminal in the same repository directory. The file contains
64 MiB of disposable zeros, not a trained model.

```sh
export CUDA_VISIBLE_DEVICES=0
MODEL_FILE=$(mktemp /tmp/gpumemd-model.XXXXXX)
dd if=/dev/zero of="$MODEL_FILE" bs=1048576 count=64
./build/gpumemctl --socket /tmp/gpumemd-quickstart.sock register_file demo "$MODEL_FILE" quickstart
./build/gpumemctl --socket /tmp/gpumemd-quickstart.sock retain demo
./build/gpumemctl --socket /tmp/gpumemd-quickstart.sock load demo

# Two separate processes import the same allocation.
./build/gpumemd-cuda-demo --socket /tmp/gpumemd-quickstart.sock --model demo
./build/gpumemd-cuda-demo --socket /tmp/gpumemd-quickstart.sock --model demo

./build/gpumemctl --socket /tmp/gpumemd-quickstart.sock release_model demo
```

Each demo should print `OK opened`, `OK cudaMemset`, and
`OK closed_on_scope_exit`. These two smoke checks run sequentially and write
zeros to the shared allocation; use only disposable data. The next step uses
concurrently alive workers that do not mutate the allocation.

The coordinator retains the model before loading and releases it after both
clients exit. This prevents eviction/unload while clients hold imported pointers.
`CudaClient::open_model()` itself does not acquire this reference.

## 4. Measure concurrent sharing

Continue in terminal B after the previous commands succeed:

```sh
python3 scripts/benchmark.py \
  --socket /tmp/gpumemd-quickstart.sock --model demo \
  --cuda-worker ./build/gpumemd-cuda-bench-worker \
  --file "$MODEL_FILE" --processes 2
python3 scripts/check_acquire_release.py --build build
```

The comparison prints independent/shared GPU memory deltas and worker timings.
For two workers and a 64 MiB file, ideal payload deltas are 128 MiB versus
64 MiB; actual measurements may include overhead. The script retains the
shared allocation until all workers close it. The acquire/release check starts
its own daemon and checks logical accounting, waiting, timeout, and cleanup.
It does not allocate CUDA memory.

## 5. Clean up

After a successful comparison, the model is already unloaded:

```sh
./build/gpumemctl --socket /tmp/gpumemd-quickstart.sock unregister demo
./build/gpumemctl --socket /tmp/gpumemd-quickstart.sock status
rm -- "$MODEL_FILE"
```

`status` should show zero usage and zero reservations. Stop terminal A with
Ctrl-C. If you skip the comparison, run `unload demo` before unregistering.
If a client fails, close remaining clients before releasing/unloading its model;
references are not automatically reclaimed when a client crashes.

## Common setup problems

- **No CUDA targets:** check `nvcc`, configuration output, and toolkit/compiler
  compatibility. Configure in a fresh build directory after fixing the toolkit.
- **Socket path already exists:** use another path or verify the previous daemon
  has stopped before removing its stale socket. Do not delete a live socket.
- **`unsupported` on `share`:** the daemon is not using CUDA. The daemon can fall
  back to mock when no usable GPU is available.
- **`model_in_use` during unload:** a retain is still active; release it only
  after the corresponding clients have closed their imported mappings.
- **Unexpected memory deltas:** stop other GPU workloads and compare repeated
  trials. Do not interpret a mock run or a negative delta as CUDA savings.

For application integration, see [the C++ client guide](CPP_INTEGRATION.md).
Report reproducible problems through [GitHub Issues](https://github.com/yahiag04/gpumemd/issues),
including the revision, compiler/toolkit versions, GPU model, and relevant logs.
