#include "gpumemd/model_registry.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using gpumemd::Bytes;
using gpumemd::ModelError;
using gpumemd::ModelRegistry;

void registers_model_with_initial_state() {
    ModelRegistry registry;
    const auto result = registry.register_model("bert", 7000000000, "bert-base");
    assert(result.ok() && result.amount == 7000000000);
    const auto snapshot = registry.models();
    assert(snapshot.models.size() == 1);
    assert(snapshot.models[0].id == "bert");
    assert(snapshot.models[0].metadata == "bert-base");
    assert(snapshot.models[0].footprint_bytes == 7000000000);
    assert(snapshot.models[0].ref_count == 0);
    assert(snapshot.models[0].last_access == 0);
}

void rejects_duplicate_and_invalid_models() {
    ModelRegistry registry;
    assert(registry.register_model("bert", 1, "base").ok());
    assert(registry.register_model("bert", 2, "other").error == ModelError::DuplicateModel);
    assert(registry.register_model("bad/id", 1, "base").error == ModelError::InvalidId);
    assert(registry.register_model("valid", 0, "base").error == ModelError::InvalidSize);
    assert(registry.register_model("valid2", 1, "").error == ModelError::InvalidMetadata);
    assert(registry.register_model("valid3", 1, "has space").error == ModelError::InvalidMetadata);
    assert(registry.register_model("valid4", 1, "has\nnewline").error == ModelError::InvalidMetadata);
    assert(registry.register_model("valid5", 1, std::string(129, 'x')).error ==
           ModelError::InvalidMetadata);
}

void retain_release_and_unregister_follow_lifecycle() {
    ModelRegistry registry;
    assert(registry.register_model("bert", 1, "base").ok());
    assert(registry.release_model("bert").error == ModelError::RefcountUnderflow);
    assert(registry.retain("bert").amount == 1);
    assert(registry.release_model("bert").amount == 0);
    assert(registry.unregister_model("bert").ok());
    assert(registry.retain("bert").error == ModelError::UnknownModel);
}

void unregister_rejects_models_in_use() {
    ModelRegistry registry;
    assert(registry.register_model("bert", 1, "base").ok());
    assert(registry.retain("bert").ok());
    assert(registry.unregister_model("bert").error == ModelError::ModelInUse);
    assert(registry.release_model("bert").ok());
    assert(registry.unregister_model("bert").ok());
}

void access_sequence_is_monotonic() {
    ModelRegistry registry;
    assert(registry.register_model("a", 1, "a").ok());
    assert(registry.register_model("b", 1, "b").ok());
    assert(registry.retain("a").ok());
    const auto first = registry.models().models[0].last_access;
    assert(registry.retain("b").ok());
    const auto second = registry.models().models[1].last_access;
    assert(registry.release_model("b").ok());
    const auto third = registry.models().models[1].last_access;
    assert(first > 0 && first < second && second < third);
}

void snapshots_are_sorted_by_id() {
    ModelRegistry registry;
    assert(registry.register_model("zeta", 1, "z").ok());
    assert(registry.register_model("alpha", 2, "a").ok());
    assert(registry.register_model("middle", 3, "m").ok());
    const auto snapshot = registry.models();
    assert(snapshot.models[0].id == "alpha");
    assert(snapshot.models[1].id == "middle");
    assert(snapshot.models[2].id == "zeta");
}

void concurrent_unique_registrations_are_safe() {
    constexpr int thread_count = 8;
    ModelRegistry registry;
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    std::vector<std::uint8_t> succeeded(thread_count, 0);

    for (int thread = 0; thread < thread_count; ++thread) {
        workers.emplace_back([&, thread] {
            const std::string id = "model_" + std::to_string(thread);
            succeeded[thread] = static_cast<std::uint8_t>(registry.register_model(id, 1, "metadata").ok());
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    for (const std::uint8_t success : succeeded) {
        assert(success);
    }
    assert(registry.models().models.size() == thread_count);
}

int main() {
    registers_model_with_initial_state();
    rejects_duplicate_and_invalid_models();
    retain_release_and_unregister_follow_lifecycle();
    unregister_rejects_models_in_use();
    access_sequence_is_monotonic();
    snapshots_are_sorted_by_id();
    concurrent_unique_registrations_are_safe();
}
