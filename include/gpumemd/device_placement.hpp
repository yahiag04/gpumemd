#pragma once

#include "gpumemd/memory.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace gpumemd {

enum class PlacementError { None, InvalidDevice, InvalidModel, InvalidSize, DuplicateModel,
                            UnknownModel, InsufficientMemory };

struct PlacementResult {
    PlacementError error{PlacementError::None};
    std::uint32_t device{0};
    [[nodiscard]] bool ok() const noexcept { return error == PlacementError::None; }
};

struct DevicePlacementRecord { std::string model_id; std::uint32_t device{0}; Bytes bytes{0}; };
struct DeviceSnapshot { std::uint32_t id{0}; Bytes capacity{0}; Bytes used{0}; Bytes free{0}; };
struct PlacementSnapshot {
    std::vector<DeviceSnapshot> devices;
    std::vector<DevicePlacementRecord> placements;
};

class DevicePlacement final {
public:
    explicit DevicePlacement(std::vector<Bytes> capacities);
    [[nodiscard]] PlacementResult place(std::string_view model_id, Bytes bytes);
    [[nodiscard]] PlacementResult release(std::string_view model_id);
    [[nodiscard]] PlacementSnapshot snapshot() const;

private:
    struct Device { Bytes capacity{0}; Bytes used{0}; };
    std::vector<Device> devices_;
    std::vector<DevicePlacementRecord> placements_;
    mutable std::mutex mutex_;
};

} // namespace gpumemd
