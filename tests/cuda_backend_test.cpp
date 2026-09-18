#include "gpumemd/cuda_backend.hpp"

#include <cassert>

int main() {
    if (!gpumemd::CUDABackend::is_available()) {
        return 0;
    }

    gpumemd::CUDABackend backend;
    const auto loaded = backend.load("cuda_probe", 4096);
    assert(loaded.ok());
    assert(loaded.amount == 4096);
    assert(backend.is_loaded("cuda_probe"));

    const auto snapshot = backend.snapshot();
    assert(snapshot.allocations.size() == 1);
    assert(snapshot.allocations[0].id == "cuda_probe");
    assert(snapshot.allocations[0].bytes == 4096);

    const auto unloaded = backend.unload("cuda_probe");
    assert(unloaded.ok());
    assert(unloaded.amount == 4096);
    assert(!backend.is_loaded("cuda_probe"));
}
