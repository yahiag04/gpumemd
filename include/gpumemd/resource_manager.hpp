#pragma once

#include "gpumemd/memory.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gpumemd {

enum class ErrorCode {
    None,
    InvalidName,
    InvalidSize,
    DuplicateClient,
    UnknownClient,
    InsufficientMemory,
    Timeout,
    InvalidTimeout,
};

struct OperationResult {
    ErrorCode error{ErrorCode::None};
    Bytes amount{0};

    [[nodiscard]] bool ok() const noexcept { return error == ErrorCode::None; }
};

struct Reservation {
    std::string name;
    Bytes bytes{0};
};

struct StatusSnapshot {
    Bytes capacity{0};
    Bytes used{0};
    Bytes free{0};
    std::vector<Reservation> reservations;
};

struct AcquireOptions {
    int priority{0};
    std::chrono::milliseconds timeout{std::chrono::milliseconds::max()};
};

class ResourceManager {
public:
    explicit ResourceManager(Bytes capacity);

    [[nodiscard]] OperationResult acquire(std::string_view name, Bytes bytes,
                                         AcquireOptions options = {});
    [[nodiscard]] OperationResult try_acquire(std::string_view name, Bytes bytes);
    [[nodiscard]] OperationResult release(std::string_view name);
    [[nodiscard]] StatusSnapshot status() const;
    void cancel_waiters() noexcept;

private:
    using Clock = std::chrono::steady_clock;

    struct PendingRequest {
        std::string name;
        Bytes bytes{0};
        int priority{0};
        std::uint64_t sequence{0};
        Clock::time_point deadline{Clock::time_point::max()};
        bool completed{false};
        OperationResult result;
    };

    static bool valid_name(std::string_view name) noexcept;
    bool has_name_locked(std::string_view name) const;
    void grant_waiters_locked(Clock::time_point now);

    const Bytes capacity_;
    Bytes used_{0};
    std::unordered_map<std::string, Bytes> reservations_;
    std::vector<std::shared_ptr<PendingRequest>> pending_;
    std::uint64_t next_sequence_{0};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
};

} // namespace gpumemd
