#pragma once

#include "gpumemd/accelerator_backend.hpp"

#include <string_view>

namespace gpumemd {

class CudaIpcClient final {
public:
    [[nodiscard]] static bool is_available() noexcept;
    [[nodiscard]] BackendOperationResult open(std::string_view token,
                                               void*& pointer) const;
    [[nodiscard]] BackendOperationResult close(void* pointer) const;
};

} // namespace gpumemd
