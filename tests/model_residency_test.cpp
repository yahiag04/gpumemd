#include "gpumemd/model_residency.hpp"
#include "gpumemd/mock_backend.hpp"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

using gpumemd::ModelError;
using gpumemd::ModelRegistry;
using gpumemd::ModelResidencyManager;
using gpumemd::MockBackend;
using gpumemd::ResourceManager;

namespace {

void loads_and_unloads_once() {
    ModelRegistry registry;
    ResourceManager resources(100);
    MockBackend backend;
    ModelResidencyManager residency(registry, resources, backend);
    assert(registry.register_model("bert", 60, "base").ok());
    assert(residency.load("bert").amount == 60);
    assert(backend.is_loaded("bert"));
    assert(resources.status().used == 60);
    assert(residency.load("bert").amount == 60);
    assert(resources.status().used == 60);
    assert(residency.unload("bert").amount == 60);
    assert(!backend.is_loaded("bert"));
    assert(resources.status().used == 0);
}

void referenced_model_cannot_unload() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("bert", 60, "base").ok());
    assert(residency.load("bert").ok());
    assert(residency.retain("bert").amount == 1);
    assert(residency.unload("bert").error == ModelError::ModelInUse);
}

void evicts_oldest_unreferenced_model() {
    ModelRegistry registry;
    ResourceManager resources(100);
    MockBackend backend;
    ModelResidencyManager residency(registry, resources, backend);
    assert(registry.register_model("a", 40, "a").ok());
    assert(registry.register_model("b", 40, "b").ok());
    assert(registry.register_model("c", 40, "c").ok());
    assert(residency.load("a").ok());
    assert(residency.load("b").ok());
    assert(residency.retain("b").ok());
    assert(residency.load("c").ok());
    assert(!backend.is_loaded("a"));
    assert(backend.is_loaded("b"));
    assert(backend.is_loaded("c"));
    const auto snapshot = residency.residency();
    assert(snapshot.records.size() == 3);
    assert(!snapshot.records[0].resident);
    assert(snapshot.records[1].resident);
    assert(snapshot.records[2].resident);
    assert(resources.status().used == 80);
}

void failed_load_leaves_existing_residency_unchanged() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("a", 40, "a").ok());
    assert(registry.register_model("b", 80, "b").ok());
    assert(residency.load("a").ok());
    assert(resources.try_acquire("filler", 30).ok());

    const auto result = residency.load("b");
    assert(result.error == ModelError::InsufficientMemory);
    const auto snapshot = residency.residency();
    assert(snapshot.records.size() == 1);
    assert(snapshot.records[0].id == "a");
    assert(snapshot.records[0].resident);
    const auto status = resources.status();
    assert(status.used == 70);
    assert(status.reservations.size() == 2);
    assert(status.reservations[0].name == "filler");
    assert(status.reservations[1].name == "model:a");
}

void supports_references_for_nonresident_models() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("known", 20, "base").ok());

    assert(residency.load("missing").error == ModelError::UnknownModel);
    assert(residency.unload("missing").error == ModelError::UnknownModel);
    assert(residency.retain("missing").error == ModelError::UnknownModel);
    assert(residency.release_model("missing").error == ModelError::UnknownModel);
    assert(residency.unload("known").error == ModelError::UnknownResidency);
    assert(residency.retain("known").amount == 1);
    auto snapshot = residency.residency();
    assert(snapshot.records.size() == 1);
    assert(snapshot.records[0].id == "known");
    assert(!snapshot.records[0].resident);
    assert(snapshot.records[0].ref_count == 1);
    assert(residency.release_model("known").amount == 0);
    snapshot = residency.residency();
    assert(snapshot.records[0].ref_count == 0);
    assert(resources.status().used == 0);
}

void unregister_rejects_resident_and_erases_nonresident_record() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("loaded", 40, "loaded").ok());
    assert(registry.register_model("cold", 20, "cold").ok());
    assert(residency.load("loaded").ok());
    assert(residency.retain("cold").ok());
    assert(residency.release_model("cold").ok());

    assert(residency.unregister_model("loaded").error == ModelError::ModelInUse);
    assert(registry.models().models.size() == 2);
    assert(resources.status().used == 40);
    assert(residency.unregister_model("cold").ok());
    assert(registry.models().models.size() == 1);
    const auto snapshot = residency.residency();
    assert(snapshot.records.size() == 1);
    assert(snapshot.records[0].id == "loaded");
}

void rejects_oversized_footprint_without_evicting() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("small", 40, "small").ok());
    assert(registry.register_model("huge", 120, "huge").ok());
    assert(residency.load("small").ok());

    assert(residency.load("huge").error == ModelError::InsufficientMemory);
    const auto snapshot = residency.residency();
    assert(snapshot.records.size() == 1);
    assert(snapshot.records[0].id == "small");
    assert(snapshot.records[0].resident);
    assert(resources.status().used == 40);
}

void rejects_release_underflow() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("bert", 20, "base").ok());
    assert(residency.load("bert").ok());
    assert(residency.release_model("bert").error == ModelError::RefcountUnderflow);
}

void reserves_the_full_logical_model_label() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    const std::string id(64, 'a');
    assert(registry.register_model(id, 20, "base").ok());
    assert(residency.load(id).ok());

    const auto status = resources.status();
    assert(status.reservations.size() == 1);
    assert(status.reservations[0].name == "model:" + id);
    assert(status.reservations[0].bytes == 20);
}

void reloading_uses_a_new_monotonic_load_order() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("a", 30, "a").ok());
    assert(registry.register_model("b", 30, "b").ok());
    assert(residency.load("a").ok());
    const auto first_load = residency.residency().records[0].last_loaded;
    assert(residency.unload("a").ok());
    assert(residency.load("b").ok());
    assert(residency.load("a").ok());

    const auto snapshot = residency.residency();
    assert(snapshot.records.size() == 2);
    assert(snapshot.records[0].last_loaded > first_load);
    assert(snapshot.records[0].last_loaded > snapshot.records[1].last_loaded);
}

void concurrent_retain_and_release_has_exact_final_refcount() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("bert", 20, "base").ok());
    assert(residency.load("bert").ok());

    constexpr std::size_t thread_count = 8;
    constexpr std::size_t operations_per_thread = 1000;
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t thread = 0; thread < thread_count; ++thread) {
        threads.emplace_back([&] {
            for (std::size_t operation = 0; operation < operations_per_thread; ++operation) {
                assert(residency.retain("bert").ok());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    assert(residency.residency().records[0].ref_count ==
           thread_count * operations_per_thread);

    threads.clear();
    for (std::size_t thread = 0; thread < thread_count; ++thread) {
        threads.emplace_back([&] {
            for (std::size_t operation = 0; operation < operations_per_thread; ++operation) {
                assert(residency.release_model("bert").ok());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    assert(residency.residency().records[0].ref_count == 0);
}

} // namespace

int main() {
    loads_and_unloads_once();
    referenced_model_cannot_unload();
    evicts_oldest_unreferenced_model();
    failed_load_leaves_existing_residency_unchanged();
    supports_references_for_nonresident_models();
    unregister_rejects_resident_and_erases_nonresident_record();
    rejects_oversized_footprint_without_evicting();
    rejects_release_underflow();
    reserves_the_full_logical_model_label();
    reloading_uses_a_new_monotonic_load_order();
    concurrent_retain_and_release_has_exact_final_refcount();
    std::cout << "model_residency_test: PASS\n";
    return 0;
}
