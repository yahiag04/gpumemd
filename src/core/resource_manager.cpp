#include "gpumemd/resource_manager.hpp"

#include <algorithm>
#include <stdexcept>

namespace gpumemd {

ResourceManager::ResourceManager(Bytes capacity) : capacity_(capacity) {
    if (capacity == 0) {
        throw std::invalid_argument("resource capacity must be positive");
    }
}

bool ResourceManager::valid_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char character) {
        const bool letter = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        return letter || digit || character == '_' || character == '-' || character == '.';
    });
}

OperationResult ResourceManager::acquire(std::string_view name, Bytes bytes) {
    if (!valid_name(name)) {
        return {ErrorCode::InvalidName, 0};
    }
    if (bytes == 0) {
        return {ErrorCode::InvalidSize, 0};
    }

    std::lock_guard lock(mutex_);
    const std::string key(name);
    if (reservations_.contains(key)) {
        return {ErrorCode::DuplicateClient, 0};
    }
    if (bytes > capacity_ - used_) {
        return {ErrorCode::InsufficientMemory, 0};
    }

    reservations_.emplace(key, bytes);
    used_ += bytes;
    return {ErrorCode::None, bytes};
}

OperationResult ResourceManager::release(std::string_view name) {
    if (!valid_name(name)) {
        return {ErrorCode::InvalidName, 0};
    }

    std::lock_guard lock(mutex_);
    const auto iterator = reservations_.find(std::string(name));
    if (iterator == reservations_.end()) {
        return {ErrorCode::UnknownClient, 0};
    }

    const Bytes bytes = iterator->second;
    reservations_.erase(iterator);
    used_ -= bytes;
    return {ErrorCode::None, bytes};
}

StatusSnapshot ResourceManager::status() const {
    std::lock_guard lock(mutex_);
    StatusSnapshot snapshot{capacity_, used_, capacity_ - used_, {}};
    snapshot.reservations.reserve(reservations_.size());
    for (const auto& [name, bytes] : reservations_) {
        snapshot.reservations.push_back({name, bytes});
    }
    std::sort(snapshot.reservations.begin(), snapshot.reservations.end(),
              [](const Reservation& left, const Reservation& right) {
                  return left.name < right.name;
              });
    return snapshot;
}

} // namespace gpumemd
