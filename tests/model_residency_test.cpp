#include "gpumemd/model_residency.hpp"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

using gpumemd::ModelError;
using gpumemd::ModelRegistry;
using gpumemd::ModelResidencyManager;
using gpumemd::ResourceManager;

namespace {

void loads_and_unloads_once() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("bert", 60, "base").ok());
    assert(residency.load("bert").amount == 60);
    assert(resources.status().used == 60);
    assert(residency.load("bert").amount == 60);
    assert(resources.status().used == 60);
    assert(residency.unload("bert").amount == 60);
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
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("a", 40, "a").ok());
    assert(registry.register_model("b", 40, "b").ok());
    assert(registry.register_model("c", 40, "c").ok());
    assert(residency.load("a").ok());
    assert(residency.load("b").ok());
    assert(residency.retain("b").ok());
    assert(residency.load("c").ok());
    const auto snapshot = residency.residency();
    assert(snapshot.records.size() == 3);
    assert(!snapshot.records[0].resident);
    assert(snapshot.records[1].resident);
    assert(snapshot.records[2].resident);
    assert(resources.status().used == 80);
}

void failed_load_rolls_back_evictions() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("a", 40, "a").ok());
    assert(registry.register_model("b", 40, "b").ok());
    assert(residency.load("a").ok());
    assert(resources.try_acquire("filler", 30).ok());
    assert(resources.try_acquire("model:b", 10).ok());

    const auto result = residency.load("b");
    assert(result.error == ModelError::InsufficientMemory);
    const auto snapshot = residency.residency();
    assert(snapshot.records.size() == 1);
    assert(snapshot.records[0].id == "a");
    assert(snapshot.records[0].resident);
    assert(resources.status().used == 80);
}

void rejects_unknown_and_nonresident_operations() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    assert(registry.register_model("known", 20, "base").ok());

    assert(residency.load("missing").error == ModelError::UnknownModel);
    assert(residency.unload("missing").error == ModelError::UnknownModel);
    assert(residency.retain("missing").error == ModelError::UnknownModel);
    assert(residency.release_model("missing").error == ModelError::UnknownModel);
    assert(residency.unload("known").error == ModelError::UnknownResidency);
    assert(residency.retain("known").error == ModelError::UnknownResidency);
    assert(residency.release_model("known").error == ModelError::UnknownResidency);
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
    failed_load_rolls_back_evictions();
    rejects_unknown_and_nonresident_operations();
    rejects_oversized_footprint_without_evicting();
    rejects_release_underflow();
    reserves_the_full_logical_model_label();
    reloading_uses_a_new_monotonic_load_order();
    concurrent_retain_and_release_has_exact_final_refcount();
    std::cout << "model_residency_test: PASS\n";
    return 0;
}
