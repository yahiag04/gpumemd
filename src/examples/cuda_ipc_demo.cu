#include "gpumemd/cuda_client.hpp"

#include <cuda_runtime_api.h>

#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cerr << "Usage: gpumemd-cuda-demo --socket PATH --model NAME\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string socket_path;
    std::string model;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if ((argument == "--socket" || argument == "--model") && index + 1 < argc) {
            const std::string value{argv[++index]};
            if (argument == "--socket") {
                socket_path = value;
            } else {
                model = value;
            }
        } else {
            print_usage();
            return 2;
        }
    }
    if (socket_path.empty() || model.empty()) {
        print_usage();
        return 2;
    }

    gpumemd::CudaClient client(socket_path);
    auto shared = client.open_model(model);
    if (!shared.ok()) {
        std::cerr << "gpumemd-cuda-demo: could not open model: "
                  << (shared.response.payload.empty() ? "CUDA IPC failure"
                                                       : shared.response.payload);
        return 1;
    }

    const cudaError_t memset_result =
        cudaMemset(shared.allocation->pointer(), 0, shared.allocation->bytes());
    if (memset_result != cudaSuccess) {
        std::cerr << "gpumemd-cuda-demo: cudaMemset failed: "
                  << cudaGetErrorString(memset_result) << '\n';
        return 1;
    }
    const cudaError_t sync_result = cudaDeviceSynchronize();
    if (sync_result != cudaSuccess) {
        std::cerr << "gpumemd-cuda-demo: CUDA synchronization failed: "
                  << cudaGetErrorString(sync_result) << '\n';
        return 1;
    }

    std::cout << "OK opened " << model << ' ' << shared.allocation->bytes() << " bytes\n"
              << "OK cudaMemset\n"
              << "OK closed_on_scope_exit\n";
    return 0;
}
