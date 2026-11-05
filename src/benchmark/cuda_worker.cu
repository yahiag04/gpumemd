#include "gpumemd/cuda_client.hpp"
#include <cuda_runtime_api.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

void check(cudaError_t result) {
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
int main(int argc, char** argv) try {
    if (argc != 5) throw std::runtime_error("Usage: cuda-bench-worker independent|shared FILE SOCKET MODEL");
    const std::string mode = argv[1];
    if (mode != "independent" && mode != "shared") throw std::runtime_error("invalid mode");
    check(cudaSetDevice(0));
    check(cudaFree(nullptr));
    std::cout << "ready" << std::endl;
    std::string command;
    if (!std::getline(std::cin, command)) return 1;
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    void* pointer = nullptr;
    gpumemd::CudaClient client(argv[3]);
    gpumemd::CudaOpenResult shared;
    if (mode == "shared") {
        shared = client.open_model(argv[4]);
        if (!shared.ok()) throw std::runtime_error("IPC open failed");
        pointer = shared.allocation->pointer();
    } else {
        std::ifstream file(argv[2], std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0) throw std::runtime_error("invalid file");
        std::vector<char> bytes(static_cast<std::size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(bytes.data(), bytes.size())) throw std::runtime_error("read failed");
        check(cudaMalloc(&pointer, bytes.size()));
        check(cudaMemcpy(pointer, bytes.data(), bytes.size(), cudaMemcpyHostToDevice));
    }
    auto loaded = Clock::now();
    check(cudaDeviceSynchronize());
    auto synced = Clock::now();
    std::cout << "load_or_share_open_us " << std::chrono::duration<double, std::micro>(loaded-start).count()
              << " sync_us " << std::chrono::duration<double, std::micro>(synced-loaded).count() << std::endl;
    if (!std::getline(std::cin, command)) return 1;
    start = Clock::now();
    if (mode == "shared") shared.allocation.reset();
    else check(cudaFree(pointer));
    std::cout << "close_us " << std::chrono::duration<double, std::micro>(Clock::now()-start).count() << std::endl;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
