#include "gpumemd/resource_manager.hpp"

#include <algorithm>
#include <cstddef>
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

bool ResourceManager::has_name_locked(std::string_view name) const {
    const std::string key(name);
    if (reservations_.contains(key)) {
        return true;
    }
    return std::any_of(pending_.begin(), pending_.end(), [&](const auto& request) {
        return request->name == name;
    });
}

void ResourceManager::grant_waiters_locked(Clock::time_point now) {
    for (std::size_t index = 0; index < pending_.size();) {
        const auto& request = pending_[index];
        if (request->deadline != Clock::time_point::max() && now >= request->deadline) {
            request->completed = true;
            request->result = {ErrorCode::Timeout, 0};
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }

    while (true) {
        std::size_t selected = pending_.size();
        for (std::size_t index = 0; index < pending_.size(); ++index) {
            const auto& candidate = pending_[index];
            if (candidate->bytes > capacity_ - used_) {
                continue;
            }
            if (selected == pending_.size() ||
                candidate->priority > pending_[selected]->priority ||
                (candidate->priority == pending_[selected]->priority &&
                 candidate->sequence < pending_[selected]->sequence)) {
                selected = index;
            }
        }
        if (selected == pending_.size()) {
            break;
        }

        const auto request = pending_[selected];
        reservations_.emplace(request->name, request->bytes);
        used_ += request->bytes;
        request->completed = true;
        request->result = {ErrorCode::None, request->bytes};
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(selected));
    }
    condition_.notify_all();
}

OperationResult ResourceManager::try_acquire(std::string_view name, Bytes bytes) {
    if (!valid_name(name)) {
        return {ErrorCode::InvalidName, 0};
    }
    if (bytes == 0) {
        return {ErrorCode::InvalidSize, 0};
    }

    std::lock_guard lock(mutex_);
    if (has_name_locked(name)) {
        return {ErrorCode::DuplicateClient, 0};
    }
    if (bytes > capacity_ - used_) {
        return {ErrorCode::InsufficientMemory, 0};
    }

    const std::string key(name);
    reservations_.emplace(key, bytes);
    used_ += bytes;
    grant_waiters_locked(Clock::now());
    return {ErrorCode::None, bytes};
}

OperationResult ResourceManager::acquire(std::string_view name, Bytes bytes,
                                         AcquireOptions options) {
    if (!valid_name(name)) {
        return {ErrorCode::InvalidName, 0};
    }
    if (bytes == 0) {
        return {ErrorCode::InvalidSize, 0};
    }
    if (options.timeout.count() < 0) {
        return {ErrorCode::InvalidTimeout, 0};
    }
    if (bytes > capacity_) {
        return {ErrorCode::InsufficientMemory, 0};
    }
    if (options.timeout.count() == 0) {
        return try_acquire(name, bytes);
    }

    std::unique_lock lock(mutex_);
    if (has_name_locked(name)) {
        return {ErrorCode::DuplicateClient, 0};
    }

    if (pending_.empty() && bytes <= capacity_ - used_) {
        const std::string key(name);
        reservations_.emplace(key, bytes);
        used_ += bytes;
        return {ErrorCode::None, bytes};
    }

    const auto request = std::make_shared<PendingRequest>();
    request->name = name;
    request->bytes = bytes;
    request->priority = options.priority;
    request->sequence = next_sequence_++;
    const auto now = Clock::now();
    if (options.timeout != std::chrono::milliseconds::max()) {
        const auto remaining = Clock::duration::max() - now.time_since_epoch();
        const auto maximum_timeout =
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
        if (options.timeout <= maximum_timeout) {
            request->deadline = now +
                                std::chrono::duration_cast<Clock::duration>(options.timeout);
        }
    }
    pending_.push_back(request);
    grant_waiters_locked(now);

    while (!request->completed) {
        grant_waiters_locked(Clock::now());
        if (request->completed) {
            break;
        }
        if (request->deadline == Clock::time_point::max()) {
            condition_.wait(lock);
        } else if (condition_.wait_until(lock, request->deadline) ==
                   std::cv_status::timeout && !request->completed) {
            const auto iterator = std::find(pending_.begin(), pending_.end(), request);
            if (iterator != pending_.end()) {
                pending_.erase(iterator);
            }
            request->completed = true;
            request->result = {ErrorCode::Timeout, 0};
        }
    }
    return request->result;
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
    grant_waiters_locked(Clock::now());
    return {ErrorCode::None, bytes};
}

void ResourceManager::cancel_waiters() noexcept {
    std::lock_guard lock(mutex_);
    for (const auto& request : pending_) {
        request->completed = true;
        request->result = {ErrorCode::Timeout, 0};
    }
    pending_.clear();
    condition_.notify_all();
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
