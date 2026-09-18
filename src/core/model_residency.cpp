#include "gpumemd/model_residency.hpp"

#include "gpumemd/mock_backend.hpp"
#include "gpumemd/model_loader.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace gpumemd {

ModelResidencyManager::ModelResidencyManager(ModelRegistry& registry,
                                             ResourceManager& resources)
    : registry_(registry), resources_(resources),
      owned_backend_(std::make_unique<MockBackend>()), backend_(*owned_backend_) {}

ModelResidencyManager::ModelResidencyManager(ModelRegistry& registry,
                                             ResourceManager& resources,
                                             AcceleratorBackend& backend)
    : registry_(registry), resources_(resources), backend_(backend) {}

ModelRecord* ModelResidencyManager::find_model(ModelSnapshot& snapshot, std::string_view id) {
    const auto iterator = std::find_if(snapshot.models.begin(), snapshot.models.end(),
                                       [id](const ModelRecord& model) {
                                           return model.id == id;
                                       });
    return iterator == snapshot.models.end() ? nullptr : &*iterator;
}

ModelOperationResult ModelResidencyManager::load(std::string_view id) {
    std::lock_guard lock(mutex_);
    auto models = registry_.models();
    const auto* model = find_model(models, id);
    if (model == nullptr) {
        return {ModelError::UnknownModel, 0};
    }

    const auto existing = records_.find(model->id);
    if (existing != records_.end() && existing->second.resident) {
        return {ModelError::None, existing->second.footprint_bytes};
    }

    const auto resource_status = resources_.status();
    if (model->footprint_bytes > resource_status.capacity) {
        return {ModelError::InsufficientMemory, 0};
    }

    std::vector<ResidencyRecord*> evictions;
    Bytes available = resource_status.free;
    if (available < model->footprint_bytes) {
        for (auto& [record_id, record] : records_) {
            (void)record_id;
            if (record.resident && record.ref_count == 0) {
                evictions.push_back(&record);
            }
        }
        std::sort(evictions.begin(), evictions.end(),
                  [](const ResidencyRecord* left, const ResidencyRecord* right) {
                      const auto score = [](const ResidencyRecord* record) {
                          const auto cost = std::max<std::uint64_t>(
                              record->estimated_load_cost_ms, 1);
                          const auto history = record->load_count + 1;
                          return static_cast<long double>(record->last_loaded) * cost * history /
                                 std::max<Bytes>(record->footprint_bytes, 1);
                      };
                      return score(left) < score(right);
                  });
        std::size_t needed_count = 0;
        while (available < model->footprint_bytes && needed_count < evictions.size()) {
            available += evictions[needed_count]->footprint_bytes;
            ++needed_count;
        }
        if (available < model->footprint_bytes) {
            return {ModelError::InsufficientMemory, 0};
        }
        evictions.resize(needed_count);
    } else {
        evictions.clear();
    }

    std::vector<std::string> eviction_ids;
    eviction_ids.reserve(evictions.size());
    for (const auto* record : evictions) {
        eviction_ids.push_back(record->id);
    }

    const auto load_started = std::chrono::steady_clock::now();
    ModelLoadResult file_data;
    BackendOperationResult loaded;
    if (model->source_path.empty()) {
        loaded = backend_.load(model->id, model->footprint_bytes);
    } else {
        ModelLoader loader;
        file_data = loader.load(model->source_path, model->footprint_bytes);
        if (!file_data.ok()) {
            switch (file_data.error) {
            case ModelLoadError::Unavailable:
                return {ModelError::FileUnavailable, 0};
            case ModelLoadError::SourceChanged:
                return {ModelError::SourceChanged, 0};
            case ModelLoadError::ReadFailure:
                return {ModelError::ReadFailure, 0};
            case ModelLoadError::None:
                break;
            }
        }
        loaded = backend_.load(model->id, file_data.data);
    }
    if (!loaded.ok()) {
        return {ModelError::BackendFailure, 0};
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - load_started);
    const auto load_cost_ms = std::max<std::uint64_t>(
        static_cast<std::uint64_t>(elapsed.count()), 1);

    const auto acquired = resources_.replace_model_reservations(
        eviction_ids, model->id, model->footprint_bytes);
    if (!acquired.ok()) {
        (void)backend_.unload(model->id);
        return {ModelError::InsufficientMemory, 0};
    }

    for (auto* record : evictions) {
        (void)backend_.unload(record->id);
        record->resident = false;
    }
    auto& record = records_[model->id];
    record.id = model->id;
    record.footprint_bytes = model->footprint_bytes;
    record.ref_count = model->ref_count;
    record.resident = true;
    record.last_loaded = ++next_loaded_;
    ++record.load_count;
    record.estimated_load_cost_ms = load_cost_ms;
    return {ModelError::None, model->footprint_bytes};
}

ModelOperationResult ModelResidencyManager::unload(std::string_view id) {
    std::lock_guard lock(mutex_);
    auto models = registry_.models();
    if (find_model(models, id) == nullptr) {
        return {ModelError::UnknownModel, 0};
    }
    const auto iterator = records_.find(std::string(id));
    if (iterator == records_.end() || !iterator->second.resident) {
        return {ModelError::UnknownResidency, 0};
    }
    if (iterator->second.ref_count != 0) {
        return {ModelError::ModelInUse, 0};
    }

    const auto unloaded = backend_.unload(id);
    if (!unloaded.ok()) {
        return {ModelError::BackendFailure, 0};
    }

    const auto released = resources_.release_model(id);
    if (!released.ok()) {
        (void)backend_.load(id, iterator->second.footprint_bytes);
        return {ModelError::UnknownResidency, 0};
    }
    iterator->second.resident = false;
    return {ModelError::None, iterator->second.footprint_bytes};
}

ModelShareResult ModelResidencyManager::share(std::string_view id) {
    std::lock_guard lock(mutex_);
    auto models = registry_.models();
    const auto* model = find_model(models, id);
    if (model == nullptr) {
        return {ModelError::UnknownModel, 0, {}};
    }
    const auto iterator = records_.find(std::string(id));
    if (iterator == records_.end() || !iterator->second.resident) {
        return {ModelError::UnknownResidency, 0, {}};
    }

    const auto shared = backend_.share(id);
    if (!shared.ok()) {
        if (shared.error == BackendError::Unsupported) {
            return {ModelError::BackendUnsupported, 0, {}};
        }
        return {ModelError::BackendFailure, 0, {}};
    }
    return {ModelError::None, shared.amount, shared.token};
}

ModelOperationResult ModelResidencyManager::retain(std::string_view id) {
    std::lock_guard lock(mutex_);
    auto models = registry_.models();
    const auto* model = find_model(models, id);
    if (model == nullptr) {
        return {ModelError::UnknownModel, 0};
    }

    const auto result = registry_.retain(id);
    if (result.ok()) {
        const auto [iterator, inserted] = records_.try_emplace(
            model->id, ResidencyRecord{model->id, model->footprint_bytes,
                                       result.amount, false, 0});
        if (!inserted) {
            iterator->second.ref_count = result.amount;
        }
    }
    return result;
}

ModelOperationResult ModelResidencyManager::release_model(std::string_view id) {
    std::lock_guard lock(mutex_);
    auto models = registry_.models();
    const auto* model = find_model(models, id);
    if (model == nullptr) {
        return {ModelError::UnknownModel, 0};
    }

    const auto result = registry_.release_model(id);
    if (result.ok()) {
        const auto [iterator, inserted] = records_.try_emplace(
            model->id, ResidencyRecord{model->id, model->footprint_bytes,
                                       result.amount, false, 0});
        if (!inserted) {
            iterator->second.ref_count = result.amount;
        }
    }
    return result;
}

ModelOperationResult ModelResidencyManager::unregister_model(std::string_view id) {
    std::lock_guard lock(mutex_);
    const auto iterator = records_.find(std::string(id));
    if (iterator != records_.end() && iterator->second.resident) {
        return {ModelError::ModelInUse, 0};
    }

    const auto result = registry_.unregister_model(id);
    if (result.ok()) {
        records_.erase(std::string(id));
    }
    return result;
}

ResidencySnapshot ModelResidencyManager::residency() const {
    std::lock_guard lock(mutex_);
    ResidencySnapshot snapshot;
    snapshot.records.reserve(records_.size());
    for (const auto& [id, record] : records_) {
        (void)id;
        snapshot.records.push_back(record);
    }
    std::sort(snapshot.records.begin(), snapshot.records.end(),
              [](const ResidencyRecord& left, const ResidencyRecord& right) {
                  return left.id < right.id;
              });
    return snapshot;
}

} // namespace gpumemd
