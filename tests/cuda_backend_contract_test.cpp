// Compile the actual host-only backend against a controllable runtime seam.
#include "../src/backend/cuda_backend.cu"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <future>
#include <unordered_set>

namespace {
std::mutex runtime_mutex;
std::unordered_set<void*> live_allocations;
bool block_copy = false, fail_copy = false, fail_allocation = false, fail_sync = false;
std::promise<void> copy_started, resume_copy;
auto copy_gate = resume_copy.get_future().share();
}

cudaError_t cudaGetDeviceCount(int* count) { *count = 1; return cudaSuccess; }
cudaError_t cudaMemGetInfo(std::size_t* free, std::size_t* total) {
    *free = *total = 1024 * 1024;
    return cudaSuccess;
}
cudaError_t cudaMalloc(void** pointer, std::size_t bytes) {
    if (fail_allocation) return cudaErrorUnknown;
    *pointer = std::malloc(bytes);
    if (!*pointer) return cudaErrorUnknown;
    std::lock_guard lock(runtime_mutex);
    live_allocations.insert(*pointer);
    return cudaSuccess;
}
cudaError_t cudaFree(void* pointer) {
    std::lock_guard lock(runtime_mutex);
    assert(live_allocations.erase(pointer) == 1);
    std::free(pointer);
    return cudaSuccess;
}
cudaError_t cudaMemcpy(void* destination, const void* source, std::size_t bytes,
                       cudaMemcpyKind) {
    if (block_copy) {
        copy_started.set_value();
        copy_gate.wait();
    }
    if (fail_copy) return cudaErrorUnknown;
    std::memcpy(destination, source, bytes);
    return cudaSuccess;
}
cudaError_t cudaIpcGetMemHandle(cudaIpcMemHandle_t* handle, void* pointer) {
    std::memcpy(handle->bytes, &pointer, sizeof(pointer));
    return cudaSuccess;
}
cudaError_t cudaStreamSynchronize(cudaStream_t) {
    return fail_sync ? cudaErrorUnknown : cudaSuccess;
}

int main() {
    using namespace gpumemd;
    CUDABackend backend;
    const std::vector<std::byte> bytes(4096, std::byte{0x5a});
    assert(backend.load("other", 4096).ok());
    block_copy = true;
    auto copying = std::async(std::launch::async, [&] { return backend.load("pending", bytes); });
    assert(copy_started.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    auto probing = std::async(std::launch::async, [&] {
        bool okay = backend.share("pending").error == BackendError::NotLoaded;
        okay &= !backend.is_loaded("pending");
        const auto snapshot = backend.snapshot();
        okay &= snapshot.allocations.size() == 1 && snapshot.allocations[0].id == "other";
        okay &= backend.load("pending", 4096).error == BackendError::AlreadyLoaded;
        okay &= backend.unload("pending").error == BackendError::NotLoaded;
        okay &= backend.share("other").ok();
        okay &= backend.unload("other").ok();
        return okay;
    });
    const bool responsive = probing.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    resume_copy.set_value(); // Always unblock before checking, even on the old implementation.
    const auto loaded = copying.get();
    const bool isolated = probing.get();
    assert(responsive && "backend mutex held during blocked copy");
    assert(isolated && "loading allocation was visible or unrelated operations failed");
    assert(loaded.ok() && backend.is_loaded("pending"));
    {
        std::lock_guard lock(runtime_mutex);
        assert(live_allocations.size() == 1);
        assert(std::memcmp(*live_allocations.begin(), bytes.data(), bytes.size()) == 0);
    }
    assert(backend.share("pending").ok());
    assert(backend.unload("pending").ok());
    block_copy = false;
    fail_copy = true;
    assert(backend.load("retry", bytes).error == BackendError::RuntimeFailure);
    assert(!backend.is_loaded("retry") && backend.snapshot().allocations.empty());
    assert(live_allocations.empty());
    fail_copy = false;
    fail_sync = true;
    assert(backend.load("retry", bytes).error == BackendError::RuntimeFailure);
    assert(backend.snapshot().allocations.empty() && live_allocations.empty());
    fail_sync = false;
    fail_allocation = true;
    assert(backend.load("retry", bytes).error == BackendError::AllocationFailed);
    assert(backend.snapshot().allocations.empty());
    fail_allocation = false;
    assert(backend.load("retry", bytes).ok());
    assert(backend.unload("retry").ok());
    assert(live_allocations.empty());
}
