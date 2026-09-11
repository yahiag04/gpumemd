#pragma once

#include "gpumemd/memory.hpp"

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

class ResourceManager {
public:
    explicit ResourceManager(Bytes capacity);

    [[nodiscard]] OperationResult acquire(std::string_view name, Bytes bytes);
    [[nodiscard]] OperationResult release(std::string_view name);
    [[nodiscard]] StatusSnapshot status() const;

private:
    static bool valid_name(std::string_view name) noexcept;

    const Bytes capacity_;
    Bytes used_{0};
    std::unordered_map<std::string, Bytes> reservations_;
    mutable std::mutex mutex_;
};

} // namespace gpumemd
