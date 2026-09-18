#include "gpumemd/device_placement.hpp"

#include <algorithm>
#include <stdexcept>

namespace gpumemd {

DevicePlacement::DevicePlacement(std::vector<Bytes> capacities) {
    if (capacities.empty() || std::any_of(capacities.begin(), capacities.end(),
                                          [](Bytes value) { return value == 0; })) {
        throw std::invalid_argument("device placement requires positive capacities");
    }
    for (const Bytes capacity : capacities) devices_.push_back({capacity, 0});
}

PlacementResult DevicePlacement::place(std::string_view model_id, Bytes bytes) {
    if (model_id.empty() || model_id.size() > 64) return {PlacementError::InvalidModel, 0};
    if (bytes == 0) return {PlacementError::InvalidSize, 0};
    std::lock_guard lock(mutex_);
    if (std::any_of(placements_.begin(), placements_.end(), [model_id](const auto& item) {
            return item.model_id == model_id;
        })) return {PlacementError::DuplicateModel, 0};
    std::size_t selected = devices_.size();
    for (std::size_t index = 0; index < devices_.size(); ++index) {
        if (bytes <= devices_[index].capacity - devices_[index].used &&
            (selected == devices_.size() || devices_[index].used < devices_[selected].used)) {
            selected = index;
        }
    }
    if (selected == devices_.size()) return {PlacementError::InsufficientMemory, 0};
    devices_[selected].used += bytes;
    placements_.push_back({std::string(model_id), static_cast<std::uint32_t>(selected), bytes});
    return {PlacementError::None, static_cast<std::uint32_t>(selected)};
}

PlacementResult DevicePlacement::release(std::string_view model_id) {
    std::lock_guard lock(mutex_);
    const auto iterator = std::find_if(placements_.begin(), placements_.end(),
                                       [model_id](const auto& item) {
                                           return item.model_id == model_id;
                                       });
    if (iterator == placements_.end()) return {PlacementError::UnknownModel, 0};
    devices_[iterator->device].used -= iterator->bytes;
    const auto device = iterator->device;
    placements_.erase(iterator);
    return {PlacementError::None, device};
}

PlacementSnapshot DevicePlacement::snapshot() const {
    std::lock_guard lock(mutex_);
    PlacementSnapshot snapshot;
    for (std::size_t index = 0; index < devices_.size(); ++index) {
        snapshot.devices.push_back({static_cast<std::uint32_t>(index), devices_[index].capacity,
                                    devices_[index].used,
                                    devices_[index].capacity - devices_[index].used});
    }
    snapshot.placements = placements_;
    std::sort(snapshot.placements.begin(), snapshot.placements.end(),
              [](const auto& left, const auto& right) { return left.model_id < right.model_id; });
    return snapshot;
}

} // namespace gpumemd
