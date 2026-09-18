#include "gpumemd/mock_backend.hpp"

#include <cassert>

using gpumemd::BackendError;
using gpumemd::MockBackend;

void loads_and_lists_allocations() {
    MockBackend backend;
    const auto loaded = backend.load("bert", 7'000);

    assert(loaded.ok());
    assert(loaded.amount == 7'000);
    assert(backend.is_loaded("bert"));

    const auto snapshot = backend.snapshot();
    assert(snapshot.allocations.size() == 1);
    assert(snapshot.allocations[0].id == "bert");
    assert(snapshot.allocations[0].bytes == 7'000);
}

void rejects_invalid_and_duplicate_loads() {
    MockBackend backend;
    assert(backend.load("bad/id", 1).error == BackendError::InvalidModelId);
    assert(backend.load("bert", 0).error == BackendError::InvalidSize);
    assert(backend.load("bert", 1).ok());
    assert(backend.load("bert", 1).error == BackendError::AlreadyLoaded);
}

void unloads_allocations_and_rejects_unknown_models() {
    MockBackend backend;
    assert(backend.unload("bert").error == BackendError::NotLoaded);
    assert(backend.load("bert", 7'000).ok());
    const auto unloaded = backend.unload("bert");
    assert(unloaded.ok());
    assert(unloaded.amount == 7'000);
    assert(!backend.is_loaded("bert"));
}

int main() {
    loads_and_lists_allocations();
    rejects_invalid_and_duplicate_loads();
    unloads_allocations_and_rejects_unknown_models();
}
