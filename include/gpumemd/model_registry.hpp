#pragma once

#include "gpumemd/memory.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gpumemd {

enum class ModelError {
    None,
    InvalidId,
    InvalidMetadata,
    InvalidSize,
    DuplicateModel,
    UnknownModel,
    ModelInUse,
    RefcountUnderflow,
    UnknownResidency,
    InsufficientMemory,
    BackendFailure,
};

struct ModelOperationResult {
    ModelError error{ModelError::None};
    Bytes amount{0};

    [[nodiscard]] bool ok() const noexcept { return error == ModelError::None; }
};

struct ModelRecord {
    std::string id;
    std::string metadata;
    Bytes footprint_bytes{0};
    std::uint64_t ref_count{0};
    std::uint64_t last_access{0};
};

struct ModelSnapshot {
    std::vector<ModelRecord> models;
};

class ModelRegistry {
public:
    [[nodiscard]] ModelOperationResult register_model(std::string_view id,
                                                       Bytes footprint_bytes,
                                                       std::string_view metadata);
    [[nodiscard]] ModelOperationResult unregister_model(std::string_view id);
    [[nodiscard]] ModelOperationResult retain(std::string_view id);
    [[nodiscard]] ModelOperationResult release_model(std::string_view id);
    [[nodiscard]] ModelSnapshot models() const;

private:
    static bool valid_id(std::string_view id) noexcept;
    static bool valid_metadata(std::string_view metadata) noexcept;

    std::unordered_map<std::string, ModelRecord> models_;
    std::uint64_t next_access_{0};
    mutable std::mutex mutex_;
};

} // namespace gpumemd
