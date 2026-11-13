// A/B experiment: original pageable file load versus two pinned staging buffers.
// No changes to the broker's loading policy; measure on the target GPU first.
#include <cuda_runtime_api.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void check(cudaError_t result) {
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
std::size_t positive(std::string_view text) {
    std::size_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0)
        throw std::runtime_error("expected a positive integer");
    return value;
}
struct DeviceBuffer {
    void* data = nullptr;
    ~DeviceBuffer() { if (data) (void)cudaFree(data); }
};
struct Slot {
    void* host = nullptr;
    cudaStream_t stream = nullptr;
    cudaEvent_t start = nullptr, end = nullptr;
    bool pending = false;
    ~Slot() {
        if (pending) (void)cudaEventSynchronize(end);
        if (stream) (void)cudaStreamSynchronize(stream);
        if (host) (void)cudaFreeHost(host);
        if (start) (void)cudaEventDestroy(start);
        if (end) (void)cudaEventDestroy(end);
        if (stream) (void)cudaStreamDestroy(stream);
    }
};
void read_exact(std::ifstream& file, void* target, std::size_t bytes) {
    if (!file.read(static_cast<char*>(target), static_cast<std::streamsize>(bytes)))
        throw std::runtime_error("file read failed or source changed");
}

void trial(const std::string& path, std::size_t bytes, std::size_t chunk_bytes,
           bool pinned, std::size_t iteration) {
    DeviceBuffer device;
    std::array<Slot, 2> slots;
    std::vector<char> pageable;
    double read_ms = 0, copy_gpu_ms = 0, submit_ms = 0, wait_ms = 0;
    const auto total_start = Clock::now();
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open input file");
    auto started = Clock::now();
    if (!pinned) pageable.resize(bytes); // Includes the original vector initialization cost.
    for (std::size_t i = 0; i < (pinned ? 2U : 1U); ++i) {
        auto& slot = slots[i];
        if (pinned) {
            check(cudaMallocHost(&slot.host, std::min(bytes, chunk_bytes)));
            check(cudaStreamCreateWithFlags(&slot.stream, cudaStreamNonBlocking));
        }
        check(cudaEventCreate(&slot.start));
        check(cudaEventCreate(&slot.end));
    }
    const double host_setup_ms = elapsed(started);
    started = Clock::now();
    check(cudaMalloc(&device.data, bytes));
    const double allocation_ms = elapsed(started);
    const auto finish = [&](Slot& slot) {
        if (!slot.pending) return;
        const auto wait_start = Clock::now();
        check(cudaEventSynchronize(slot.end));
        wait_ms += elapsed(wait_start);
        float copy_ms = 0;
        check(cudaEventElapsedTime(&copy_ms, slot.start, slot.end));
        copy_gpu_ms += copy_ms;
        slot.pending = false;
    };
    const std::size_t step = pinned ? std::min(bytes, chunk_bytes) : bytes;
    std::size_t chunk = 0;
    for (std::size_t offset = 0; offset < bytes; ++chunk) {
        auto& slot = slots[pinned ? chunk % 2 : 0];
        finish(slot); // Never overwrite a pinned buffer while its DMA is in flight.
        const auto count = std::min(step, bytes - offset);
        void* host = pinned ? slot.host : pageable.data();
        started = Clock::now();
        read_exact(file, host, count);
        read_ms += elapsed(started);
        started = Clock::now();
        check(cudaEventRecord(slot.start, slot.stream));
        if (pinned) {
            check(cudaMemcpyAsync(static_cast<char*>(device.data) + offset, host, count,
                                  cudaMemcpyHostToDevice, slot.stream));
        } else {
            check(cudaMemcpy(device.data, host, count, cudaMemcpyHostToDevice));
        }
        check(cudaEventRecord(slot.end, slot.stream));
        slot.pending = true;
        submit_ms += elapsed(started);
        offset += count;
    }
    for (auto& slot : slots) finish(slot);
    const double total_ms = elapsed(total_start);

    // Full byte-for-byte readback outside the measured interval, bounded host RAM.
    file.clear();
    file.seekg(0);
    const auto verify_chunk = std::min(bytes, chunk_bytes);
    std::vector<char> expected(verify_chunk), received(verify_chunk);
    for (std::size_t offset = 0; offset < bytes;) {
        const auto count = std::min(verify_chunk, bytes - offset);
        read_exact(file, expected.data(), count);
        check(cudaMemcpy(received.data(), static_cast<char*>(device.data) + offset,
                         count, cudaMemcpyDeviceToHost));
        if (std::memcmp(expected.data(), received.data(), count) != 0)
            throw std::runtime_error("GPU readback differs from input file");
        offset += count;
    }
    if (std::filesystem::file_size(path) != bytes)
        throw std::runtime_error("source size changed during trial");
    std::cout << "{\"mode\":\"" << (pinned ? "pinned_pipeline" : "pageable")
              << "\",\"trial\":" << iteration << ",\"bytes\":" << bytes
              << ",\"chunk_bytes\":" << step << ",\"total_ms\":" << total_ms
              << ",\"host_setup_ms\":" << host_setup_ms
              << ",\"device_alloc_ms\":" << allocation_ms << ",\"file_read_ms\":" << read_ms
              << ",\"h2d_gpu_ms\":" << copy_gpu_ms << ",\"submit_wall_ms\":" << submit_ms
              << ",\"wait_ms\":" << wait_ms << ",\"verified\":true}" << std::endl;
}
} // namespace

int main(int argc, char** argv) try {
    std::string path;
    std::size_t iterations = 3, chunk_mib = 8;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help") {
            std::cout << "Usage: gpumemd-cuda-load-bench --file PATH [--iterations N] [--chunk-mib N]\n";
            return 0;
        }
        if (++i >= argc) throw std::runtime_error("missing option value");
        if (arg == "--file") path = argv[i];
        else if (arg == "--iterations") iterations = positive(argv[i]);
        else if (arg == "--chunk-mib") chunk_mib = positive(argv[i]);
        else throw std::runtime_error("unknown option");
    }
    if (path.empty() || !std::filesystem::is_regular_file(path))
        throw std::runtime_error("--file must name a regular, nonempty file");
    const auto size = std::filesystem::file_size(path);
    if (size == 0 || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
        size > std::numeric_limits<std::size_t>::max() ||
        chunk_mib > std::numeric_limits<std::size_t>::max() / (1024 * 1024))
        throw std::runtime_error("file or chunk size out of range");
    check(cudaSetDevice(0));
    check(cudaFree(nullptr)); // Exclude CUDA context initialization in both modes.
    for (std::size_t i = 0; i < iterations; ++i) {
        // Alternate order to reduce a systematic first-run/cache advantage.
        trial(path, size, chunk_mib * 1024 * 1024, i % 2 != 0, i + 1);
        trial(path, size, chunk_mib * 1024 * 1024, i % 2 == 0, i + 1);
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << "gpumemd-cuda-load-bench: " << error.what() << '\n';
    return 1;
}
