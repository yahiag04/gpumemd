#include "gpumemd/mock_backend.hpp"

#include <algorithm>

namespace gpumemd {

bool MockBackend::valid_id(std::string_view id) noexcept {
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

BackendOperationResult MockBackend::load(std::string_view id, Bytes bytes) {
    if (!valid_id(id)) {
        return {BackendError::InvalidModelId, 0};
    }
    if (bytes == 0) {
        return {BackendError::InvalidSize, 0};
    }

    std::lock_guard lock(mutex_);
    if (allocations_.contains(std::string(id))) {
        return {BackendError::AlreadyLoaded, 0};
    }
    allocations_.emplace(std::string(id), bytes);
    return {BackendError::None, bytes};
}

BackendOperationResult MockBackend::load(std::string_view id,
                                         std::span<const std::byte> data) {
    return load(id, static_cast<Bytes>(data.size()));
}

BackendOperationResult MockBackend::unload(std::string_view id) {
    if (!valid_id(id)) {
        return {BackendError::InvalidModelId, 0};
    }

    std::lock_guard lock(mutex_);
    const auto iterator = allocations_.find(std::string(id));
    if (iterator == allocations_.end()) {
        return {BackendError::NotLoaded, 0};
    }
    const Bytes bytes = iterator->second;
    allocations_.erase(iterator);
    return {BackendError::None, bytes};
}

bool MockBackend::is_loaded(std::string_view id) const {
    std::lock_guard lock(mutex_);
    return allocations_.contains(std::string(id));
}

BackendSnapshot MockBackend::snapshot() const {
    std::lock_guard lock(mutex_);
    BackendSnapshot snapshot;
    snapshot.allocations.reserve(allocations_.size());
    for (const auto& [id, bytes] : allocations_) {
        snapshot.allocations.push_back({id, bytes});
    }
    std::sort(snapshot.allocations.begin(), snapshot.allocations.end(),
              [](const BackendAllocation& left, const BackendAllocation& right) {
                  return left.id < right.id;
              });
    return snapshot;
}

} // namespace gpumemd
