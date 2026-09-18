#include "gpumemd/cuda_backend.hpp"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gpumemd {

struct CUDABackend::Impl {
    struct Allocation {
        void* pointer{nullptr};
        Bytes bytes{0};
    };

    std::unordered_map<std::string, Allocation> allocations;
    mutable std::mutex mutex;
};

namespace {

bool valid_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char character) {
        const bool letter = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        return letter || digit || character == '_' || character == '-' || character == '.';
    });
}

} // namespace

CUDABackend::CUDABackend() : impl_(std::make_unique<Impl>()) {}

CUDABackend::~CUDABackend() {
    std::lock_guard lock(impl_->mutex);
    for (const auto& [id, allocation] : impl_->allocations) {
        (void)id;
        if (allocation.pointer != nullptr) {
            (void)cudaFree(allocation.pointer);
        }
    }
}

bool CUDABackend::is_available() noexcept {
    int device_count = 0;
    return cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0;
}

BackendOperationResult CUDABackend::load(std::string_view id, Bytes bytes) {
    if (!valid_id(id)) {
        return {BackendError::InvalidModelId, 0};
    }
    if (bytes == 0) {
        return {BackendError::InvalidSize, 0};
    }
    if (bytes > static_cast<Bytes>(std::numeric_limits<std::size_t>::max())) {
        return {BackendError::AllocationFailed, 0};
    }

    std::lock_guard lock(impl_->mutex);
    const std::string key(id);
    if (impl_->allocations.contains(key)) {
        return {BackendError::AlreadyLoaded, 0};
    }

    std::size_t free_bytes = 0;
    std::size_t total_bytes = 0;
    const auto info_result = cudaMemGetInfo(&free_bytes, &total_bytes);
    (void)total_bytes;
    if (info_result != cudaSuccess) {
        return {BackendError::RuntimeFailure, 0};
    }
    if (bytes > free_bytes) {
        return {BackendError::AllocationFailed, 0};
    }

    void* pointer = nullptr;
    const auto allocation_result = cudaMalloc(&pointer, static_cast<std::size_t>(bytes));
    if (allocation_result != cudaSuccess) {
        return {BackendError::AllocationFailed, 0};
    }
    impl_->allocations.emplace(key, Impl::Allocation{pointer, bytes});
    return {BackendError::None, bytes};
}

BackendOperationResult CUDABackend::load(std::string_view id,
                                         std::span<const std::byte> data) {
    const auto result = load(id, static_cast<Bytes>(data.size()));
    if (!result.ok()) {
        return result;
    }
    std::lock_guard lock(impl_->mutex);
    const auto iterator = impl_->allocations.find(std::string(id));
    if (iterator == impl_->allocations.end()) {
        return {BackendError::RuntimeFailure, 0};
    }
    const auto copy_result = cudaMemcpy(iterator->second.pointer, data.data(), data.size(),
                                         cudaMemcpyHostToDevice);
    if (copy_result != cudaSuccess) {
        (void)cudaFree(iterator->second.pointer);
        impl_->allocations.erase(iterator);
        return {BackendError::RuntimeFailure, 0};
    }
    return result;
}

BackendOperationResult CUDABackend::unload(std::string_view id) {
    if (!valid_id(id)) {
        return {BackendError::InvalidModelId, 0};
    }

    std::lock_guard lock(impl_->mutex);
    const auto iterator = impl_->allocations.find(std::string(id));
    if (iterator == impl_->allocations.end()) {
        return {BackendError::NotLoaded, 0};
    }
    const auto result = cudaFree(iterator->second.pointer);
    if (result != cudaSuccess) {
        return {BackendError::RuntimeFailure, 0};
    }
    const Bytes bytes = iterator->second.bytes;
    impl_->allocations.erase(iterator);
    return {BackendError::None, bytes};
}

BackendShareResult CUDABackend::share(std::string_view id) {
    std::lock_guard lock(impl_->mutex);
    const auto iterator = impl_->allocations.find(std::string(id));
    if (iterator == impl_->allocations.end()) {
        return {BackendError::NotLoaded, 0, {}};
    }

    cudaIpcMemHandle_t handle{};
    if (cudaIpcGetMemHandle(&handle, iterator->second.pointer) != cudaSuccess) {
        return {BackendError::RuntimeFailure, 0, {}};
    }
    static constexpr char digits[] = "0123456789abcdef";
    const auto* bytes = reinterpret_cast<const unsigned char*>(&handle);
    std::string token;
    token.reserve(sizeof(handle) * 2);
    for (std::size_t index = 0; index < sizeof(handle); ++index) {
        token.push_back(digits[bytes[index] >> 4]);
        token.push_back(digits[bytes[index] & 0x0f]);
    }
    return {BackendError::None, iterator->second.bytes, std::move(token)};
}

bool CUDABackend::is_loaded(std::string_view id) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->allocations.contains(std::string(id));
}

BackendSnapshot CUDABackend::snapshot() const {
    std::lock_guard lock(impl_->mutex);
    BackendSnapshot snapshot;
    snapshot.allocations.reserve(impl_->allocations.size());
    for (const auto& [id, allocation] : impl_->allocations) {
        snapshot.allocations.push_back({id, allocation.bytes});
    }
    std::sort(snapshot.allocations.begin(), snapshot.allocations.end(),
              [](const BackendAllocation& left, const BackendAllocation& right) {
                  return left.id < right.id;
              });
    return snapshot;
}

} // namespace gpumemd
