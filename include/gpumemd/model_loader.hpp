#pragma once

#include "gpumemd/memory.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gpumemd {

enum class ModelLoadError {
    None,
    Unavailable,
    SourceChanged,
    ReadFailure,
};

struct ModelLoadResult {
    ModelLoadError error{ModelLoadError::None};
    std::vector<std::byte> data;

    [[nodiscard]] bool ok() const noexcept { return error == ModelLoadError::None; }
};

class ModelLoader final {
public:
    [[nodiscard]] ModelLoadResult load(std::string_view path,
                                       Bytes expected_bytes) const;
};

} // namespace gpumemd
