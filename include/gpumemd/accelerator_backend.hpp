#pragma once

#include "gpumemd/memory.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace gpumemd {

enum class BackendError {
    None,
    InvalidModelId,
    InvalidSize,
    AlreadyLoaded,
    NotLoaded,
};

struct BackendOperationResult {
    BackendError error{BackendError::None};
    Bytes amount{0};

    [[nodiscard]] bool ok() const noexcept { return error == BackendError::None; }
};

struct BackendAllocation {
    std::string id;
    Bytes bytes{0};
};

struct BackendSnapshot {
    std::vector<BackendAllocation> allocations;
};

class AcceleratorBackend {
public:
    virtual ~AcceleratorBackend() = default;

    AcceleratorBackend(const AcceleratorBackend&) = delete;
    AcceleratorBackend& operator=(const AcceleratorBackend&) = delete;

    [[nodiscard]] virtual BackendOperationResult load(std::string_view id,
                                                       Bytes bytes) = 0;
    [[nodiscard]] virtual BackendOperationResult unload(std::string_view id) = 0;
    [[nodiscard]] virtual bool is_loaded(std::string_view id) const = 0;
    [[nodiscard]] virtual BackendSnapshot snapshot() const = 0;

protected:
    AcceleratorBackend() = default;
};

} // namespace gpumemd
