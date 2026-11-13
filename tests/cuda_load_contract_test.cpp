// Exercise buffer reuse and full readback using deferred host copies, not a GPU.
#define main benchmark_main
#include "../src/benchmark/cuda_load.cu"
#undef main

#include <cassert>
#include <cstdlib>
#include <functional>
#include <unordered_set>
#include <unistd.h>

namespace {
struct Stream { std::vector<std::function<void()>> pending; };
struct Event { Stream* stream = nullptr; };
std::unordered_set<void*> allocations;
int pinned_buffers = 0, streams = 0, events = 0;
bool corrupt_readback = false, fail_pinned = false;
}
const char* cudaGetErrorString(cudaError_t) { return "injected CUDA error"; }
cudaError_t cudaSetDevice(int) { return cudaSuccess; }
cudaError_t cudaMalloc(void** pointer, std::size_t bytes) {
    *pointer = std::malloc(bytes);
    if (!*pointer) return cudaErrorUnknown;
    allocations.insert(*pointer);
    return cudaSuccess;
}
cudaError_t cudaFree(void* pointer) {
    if (pointer) {
        assert(allocations.erase(pointer) == 1);
        std::free(pointer);
    }
    return cudaSuccess;
}
cudaError_t cudaMallocHost(void** pointer, std::size_t bytes) {
    if (fail_pinned) return cudaErrorUnknown;
    ++pinned_buffers;
    assert(pinned_buffers <= 2);
    return cudaMalloc(pointer, bytes);
}
cudaError_t cudaFreeHost(void* pointer) { --pinned_buffers; return cudaFree(pointer); }
cudaError_t cudaStreamCreateWithFlags(cudaStream_t* stream, unsigned) {
    *stream = new Stream;
    ++streams;
    return cudaSuccess;
}
cudaError_t cudaStreamSynchronize(cudaStream_t stream) {
    if (stream) {
        auto* state = static_cast<Stream*>(stream);
        for (auto& operation : state->pending) operation();
        state->pending.clear();
    }
    return cudaSuccess;
}
cudaError_t cudaStreamDestroy(cudaStream_t stream) {
    assert(static_cast<Stream*>(stream)->pending.empty());
    delete static_cast<Stream*>(stream);
    --streams;
    return cudaSuccess;
}
cudaError_t cudaMemcpy(void* to, const void* from, std::size_t bytes, cudaMemcpyKind kind) {
    std::memcpy(to, from, bytes);
    if (corrupt_readback && kind == cudaMemcpyDeviceToHost) static_cast<char*>(to)[0] ^= 1;
    return cudaSuccess;
}
cudaError_t cudaMemcpyAsync(void* to, const void* from, std::size_t bytes,
                            cudaMemcpyKind kind, cudaStream_t stream) {
    // Read the buffer only at synchronization: early reuse corrupts the result.
    static_cast<Stream*>(stream)->pending.push_back([=] { cudaMemcpy(to, from, bytes, kind); });
    return cudaSuccess;
}
cudaError_t cudaEventCreate(cudaEvent_t* event) { *event = new Event; ++events; return cudaSuccess; }
cudaError_t cudaEventDestroy(cudaEvent_t event) { delete static_cast<Event*>(event); --events; return cudaSuccess; }
cudaError_t cudaEventRecord(cudaEvent_t event, cudaStream_t stream) {
    static_cast<Event*>(event)->stream = static_cast<Stream*>(stream);
    return cudaSuccess;
}
cudaError_t cudaEventSynchronize(cudaEvent_t event) {
    return cudaStreamSynchronize(static_cast<Event*>(event)->stream);
}
cudaError_t cudaEventElapsedTime(float* ms, cudaEvent_t, cudaEvent_t) { *ms = 0; return cudaSuccess; }

int main() {
    char name[] = "/tmp/gpumemd-load-test-XXXXXX";
    const int fd = mkstemp(name);
    assert(fd >= 0);
    close(fd);
    for (const auto size : {1U, 7U, 8U, 9U, 29U}) {
        {
            std::ofstream file(name, std::ios::binary);
            for (unsigned i = 0; i < size; ++i) file.put(static_cast<char>(i));
        }
        trial(name, size, 8, false, 1);
        trial(name, size, 8, true, 1);
        assert(allocations.empty() && pinned_buffers == 0 && streams == 0 && events == 0);
    }
    corrupt_readback = true;
    bool rejected = false;
    try { trial(name, 29, 8, true, 1); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected && allocations.empty() && pinned_buffers == 0 && streams == 0 && events == 0);
    corrupt_readback = false;
    fail_pinned = true;
    rejected = false;
    try { trial(name, 29, 8, true, 1); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected && allocations.empty() && pinned_buffers == 0 && streams == 0 && events == 0);
    unlink(name);
}
