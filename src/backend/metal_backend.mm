#import <Metal/Metal.h>

#include "gpumemd/metal_backend.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gpumemd {

struct MetalBackend::Impl {
    id<MTLDevice> device{MTLCreateSystemDefaultDevice()};
    std::unordered_map<std::string, id> buffers;
    mutable std::mutex mutex;
};

MetalBackend::MetalBackend() : impl_(std::make_unique<Impl>()) {}

MetalBackend::~MetalBackend() = default;

bool MetalBackend::is_available() noexcept {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    return device != nil;
}

BackendOperationResult MetalBackend::load(std::string_view id, Bytes bytes) {
    if (id.empty() || id.size() > 64) {
        return {BackendError::InvalidModelId, 0};
    }
    if (bytes == 0) {
        return {BackendError::InvalidSize, 0};
    }
    if (impl_->device == nil) {
        return {BackendError::DeviceUnavailable, 0};
    }
    if (bytes > static_cast<Bytes>(std::numeric_limits<NSUInteger>::max())) {
        return {BackendError::AllocationFailed, 0};
    }

    std::lock_guard lock(impl_->mutex);
    const std::string key(id);
    if (impl_->buffers.contains(key)) {
        return {BackendError::AlreadyLoaded, 0};
    }

    ::id buffer = [impl_->device
        newBufferWithLength:static_cast<NSUInteger>(bytes)
                    options:MTLResourceStorageModeShared];
    if (buffer == nil) {
        return {BackendError::AllocationFailed, 0};
    }
    impl_->buffers.emplace(key, buffer);
    return {BackendError::None, bytes};
}

BackendOperationResult MetalBackend::load(std::string_view id,
                                          std::span<const std::byte> data) {
    const auto result = load(id, static_cast<Bytes>(data.size()));
    if (!result.ok()) {
        return result;
    }
    std::lock_guard lock(impl_->mutex);
    const auto iterator = impl_->buffers.find(std::string(id));
    if (iterator == impl_->buffers.end()) {
        return {BackendError::RuntimeFailure, 0};
    }
    std::memcpy([iterator->second contents], data.data(), data.size());
    return result;
}

BackendOperationResult MetalBackend::unload(std::string_view id) {
    if (id.empty() || id.size() > 64) {
        return {BackendError::InvalidModelId, 0};
    }

    std::lock_guard lock(impl_->mutex);
    const auto iterator = impl_->buffers.find(std::string(id));
    if (iterator == impl_->buffers.end()) {
        return {BackendError::NotLoaded, 0};
    }
    const Bytes bytes = [iterator->second length];
    impl_->buffers.erase(iterator);
    return {BackendError::None, bytes};
}

bool MetalBackend::is_loaded(std::string_view id) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->buffers.contains(std::string(id));
}

BackendSnapshot MetalBackend::snapshot() const {
    std::lock_guard lock(impl_->mutex);
    BackendSnapshot snapshot;
    snapshot.allocations.reserve(impl_->buffers.size());
    for (const auto& [id, buffer] : impl_->buffers) {
        snapshot.allocations.push_back({id, static_cast<Bytes>([buffer length])});
    }
    std::sort(snapshot.allocations.begin(), snapshot.allocations.end(),
              [](const BackendAllocation& left, const BackendAllocation& right) {
                  return left.id < right.id;
              });
    return snapshot;
}

} // namespace gpumemd
