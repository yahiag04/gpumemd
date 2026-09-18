#include "gpumemd/cuda_client.hpp"

#include <utility>

namespace gpumemd {

CudaIpcAllocation::CudaIpcAllocation(void* pointer, Bytes bytes)
    : pointer_(pointer), bytes_(bytes) {}

CudaIpcAllocation::~CudaIpcAllocation() { reset(); }

CudaIpcAllocation::CudaIpcAllocation(CudaIpcAllocation&& other) noexcept
    : pointer_(other.pointer_), bytes_(other.bytes_) {
    other.pointer_ = nullptr;
    other.bytes_ = 0;
}

CudaIpcAllocation& CudaIpcAllocation::operator=(CudaIpcAllocation&& other) noexcept {
    if (this != &other) {
        reset();
        pointer_ = other.pointer_;
        bytes_ = other.bytes_;
        other.pointer_ = nullptr;
        other.bytes_ = 0;
    }
    return *this;
}

void CudaIpcAllocation::reset() noexcept {
    if (pointer_ != nullptr) {
        (void)ipc_.close(pointer_);
        pointer_ = nullptr;
        bytes_ = 0;
    }
}

CudaClient::CudaClient(std::string socket_path) : broker_(std::move(socket_path)) {}

CudaOpenResult CudaClient::open_model(std::string_view name) const {
    const ClientResponse response = broker_.share(name);
    if (!response.ok()) return {response, {BackendError::None, 0}, nullptr};

    const auto descriptor = parse_share_response(response.payload);
    if (!descriptor) {
        return {{ClientError::ReadFailed, response.payload}, {BackendError::RuntimeFailure, 0},
                nullptr};
    }

    void* pointer = nullptr;
    const BackendOperationResult opened = ipc_.open(descriptor->token, pointer);
    if (!opened.ok()) return {response, opened, nullptr};
    return {response,
            opened,
            std::make_unique<CudaIpcAllocation>(pointer, descriptor->bytes)};
}

} // namespace gpumemd
