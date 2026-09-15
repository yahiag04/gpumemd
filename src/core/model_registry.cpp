#include "gpumemd/model_registry.hpp"

#include <algorithm>

namespace gpumemd {

bool ModelRegistry::valid_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char character) {
        const bool letter = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        return letter || digit || character == '_' || character == '-' || character == '.';
    });
}

bool ModelRegistry::valid_metadata(std::string_view metadata) noexcept {
    if (metadata.empty() || metadata.size() > 128) {
        return false;
    }
    return std::all_of(metadata.begin(), metadata.end(), [](char character) {
        const auto value = static_cast<unsigned char>(character);
        return value < 128 && value >= 32 && value != 127 &&
               character != ' ' && character != '\t' && character != '\n' &&
               character != '\r' && character != '\f' && character != '\v';
    });
}

ModelOperationResult ModelRegistry::register_model(std::string_view id, Bytes footprint_bytes,
                                                   std::string_view metadata) {
    if (!valid_id(id)) {
        return {ModelError::InvalidId, 0};
    }
    if (footprint_bytes == 0) {
        return {ModelError::InvalidSize, 0};
    }
    if (!valid_metadata(metadata)) {
        return {ModelError::InvalidMetadata, 0};
    }

    std::lock_guard lock(mutex_);
    if (models_.contains(std::string(id))) {
        return {ModelError::DuplicateModel, 0};
    }
    models_.emplace(std::string(id), ModelRecord{std::string(id), std::string(metadata),
                                                  footprint_bytes, 0, 0});
    return {ModelError::None, footprint_bytes};
}

ModelOperationResult ModelRegistry::unregister_model(std::string_view id) {
    if (!valid_id(id)) {
        return {ModelError::InvalidId, 0};
    }

    std::lock_guard lock(mutex_);
    const auto iterator = models_.find(std::string(id));
    if (iterator == models_.end()) {
        return {ModelError::UnknownModel, 0};
    }
    if (iterator->second.ref_count != 0) {
        return {ModelError::ModelInUse, 0};
    }
    models_.erase(iterator);
    return {ModelError::None, 0};
}

ModelOperationResult ModelRegistry::retain(std::string_view id) {
    if (!valid_id(id)) {
        return {ModelError::InvalidId, 0};
    }

    std::lock_guard lock(mutex_);
    const auto iterator = models_.find(std::string(id));
    if (iterator == models_.end()) {
        return {ModelError::UnknownModel, 0};
    }
    ++iterator->second.ref_count;
    iterator->second.last_access = ++next_access_;
    return {ModelError::None, iterator->second.ref_count};
}

ModelOperationResult ModelRegistry::release_model(std::string_view id) {
    if (!valid_id(id)) {
        return {ModelError::InvalidId, 0};
    }

    std::lock_guard lock(mutex_);
    const auto iterator = models_.find(std::string(id));
    if (iterator == models_.end()) {
        return {ModelError::UnknownModel, 0};
    }
    if (iterator->second.ref_count == 0) {
        return {ModelError::RefcountUnderflow, 0};
    }
    --iterator->second.ref_count;
    iterator->second.last_access = ++next_access_;
    return {ModelError::None, iterator->second.ref_count};
}

ModelSnapshot ModelRegistry::models() const {
    std::lock_guard lock(mutex_);
    ModelSnapshot snapshot;
    snapshot.models.reserve(models_.size());
    for (const auto& [id, record] : models_) {
        (void)id;
        snapshot.models.push_back(record);
    }
    std::sort(snapshot.models.begin(), snapshot.models.end(),
              [](const ModelRecord& left, const ModelRecord& right) {
                  return left.id < right.id;
              });
    return snapshot;
}

} // namespace gpumemd
