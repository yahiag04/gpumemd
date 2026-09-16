#pragma once

#include "gpumemd/model_registry.hpp"
#include "gpumemd/resource_manager.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gpumemd {

struct ResidencyRecord {
    std::string id;
    Bytes footprint_bytes{0};
    std::uint64_t ref_count{0};
    bool resident{false};
    std::uint64_t last_loaded{0};
};

struct ResidencySnapshot {
    std::vector<ResidencyRecord> records;
};

class ModelResidencyManager {
public:
    ModelResidencyManager(ModelRegistry& registry, ResourceManager& resources);

    [[nodiscard]] ModelOperationResult load(std::string_view id);
    [[nodiscard]] ModelOperationResult unload(std::string_view id);
    [[nodiscard]] ModelOperationResult retain(std::string_view id);
    [[nodiscard]] ModelOperationResult release_model(std::string_view id);
    [[nodiscard]] ResidencySnapshot residency() const;

private:
    [[nodiscard]] ModelRecord* find_model(ModelSnapshot& snapshot, std::string_view id);

    ModelRegistry& registry_;
    ResourceManager& resources_;
    std::unordered_map<std::string, ResidencyRecord> records_;
    std::uint64_t next_loaded_{0};
    mutable std::mutex mutex_;
};

} // namespace gpumemd
