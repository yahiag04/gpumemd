#pragma once

#include "gpumemd/accelerator_backend.hpp"

#include <memory>

namespace gpumemd {

class CUDABackend final : public AcceleratorBackend {
public:
    CUDABackend();
    ~CUDABackend() override;

    CUDABackend(const CUDABackend&) = delete;
    CUDABackend& operator=(const CUDABackend&) = delete;

    [[nodiscard]] static bool is_available() noexcept;
    [[nodiscard]] BackendOperationResult load(std::string_view id,
                                               Bytes bytes) override;
    [[nodiscard]] BackendOperationResult load(
        std::string_view id, std::span<const std::byte> data) override;
    [[nodiscard]] BackendOperationResult unload(std::string_view id) override;
    [[nodiscard]] BackendShareResult share(std::string_view id) override;
    [[nodiscard]] bool is_loaded(std::string_view id) const override;
    [[nodiscard]] BackendSnapshot snapshot() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gpumemd
