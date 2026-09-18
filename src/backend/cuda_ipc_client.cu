#include "gpumemd/cuda_ipc_client.hpp"

#include <cuda_runtime_api.h>

#include <cctype>
#include <cstddef>
#include <string>

namespace gpumemd {
namespace {

int hex_value(char character) noexcept {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

} // namespace

bool CudaIpcClient::is_available() noexcept {
    int device_count = 0;
    return cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0;
}

BackendOperationResult CudaIpcClient::open(std::string_view token, void*& pointer) const {
    if (token.size() != sizeof(cudaIpcMemHandle_t) * 2) {
        return {BackendError::InvalidSize, 0};
    }
    cudaIpcMemHandle_t handle{};
    auto* bytes = reinterpret_cast<unsigned char*>(&handle);
    for (std::size_t index = 0; index < sizeof(handle); ++index) {
        const int high = hex_value(token[index * 2]);
        const int low = hex_value(token[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return {BackendError::RuntimeFailure, 0};
        }
        bytes[index] = static_cast<unsigned char>((high << 4) | low);
    }
    if (cudaIpcOpenMemHandle(&pointer, handle, cudaIpcMemLazyEnablePeerAccess) != cudaSuccess) {
        pointer = nullptr;
        return {BackendError::RuntimeFailure, 0};
    }
    return {BackendError::None, 0};
}

BackendOperationResult CudaIpcClient::close(void* pointer) const {
    if (pointer == nullptr || cudaIpcCloseMemHandle(pointer) != cudaSuccess) {
        return {BackendError::RuntimeFailure, 0};
    }
    return {BackendError::None, 0};
}

} // namespace gpumemd
