#pragma once

#include "gpumemd/accelerator_backend.hpp"

#include <memory>

namespace gpumemd {

class MetalBackend final : public AcceleratorBackend {
public:
    MetalBackend();
    ~MetalBackend() override;

    MetalBackend(const MetalBackend&) = delete;
    MetalBackend& operator=(const MetalBackend&) = delete;

    [[nodiscard]] static bool is_available() noexcept;
    [[nodiscard]] BackendOperationResult load(std::string_view id,
                                               Bytes bytes) override;
    [[nodiscard]] BackendOperationResult unload(std::string_view id) override;
    [[nodiscard]] bool is_loaded(std::string_view id) const override;
    [[nodiscard]] BackendSnapshot snapshot() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gpumemd
