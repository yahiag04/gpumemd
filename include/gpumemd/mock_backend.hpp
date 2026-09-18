#pragma once

#include "gpumemd/accelerator_backend.hpp"

#include <mutex>
#include <unordered_map>

namespace gpumemd {

class MockBackend final : public AcceleratorBackend {
public:
    [[nodiscard]] BackendOperationResult load(std::string_view id,
                                               Bytes bytes) override;
    [[nodiscard]] BackendOperationResult load(
        std::string_view id, std::span<const std::byte> data) override;
    [[nodiscard]] BackendOperationResult unload(std::string_view id) override;
    [[nodiscard]] bool is_loaded(std::string_view id) const override;
    [[nodiscard]] BackendSnapshot snapshot() const override;

private:
    static bool valid_id(std::string_view id) noexcept;

    std::unordered_map<std::string, Bytes> allocations_;
    mutable std::mutex mutex_;
};

} // namespace gpumemd
