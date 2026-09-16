# v0.4 Model Residency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (\`- [ ]\`) syntax for tracking.

**Goal:** Add explicit simulated model load/unload operations with reference-aware LRU eviction while preserving the independent v0.3 registry and resource accounting cores.

**Architecture:** Add ModelResidencyManager as the only coordinator between ModelRegistry and ResourceManager. It owns a mutex, resident records, synthetic model reservation labels, and load-order sequence; all model retain/release/load/unload operations served by IPC route through it.

**Tech Stack:** C++20, CMake 3.20+, POSIX Unix domain sockets, CTest, standard library only.

**Spec:** docs/superpowers/specs/2026-09-16-v04-residency-design.md

## Global Constraints

- v0.4 simulates residency only; no model bytes, CUDA/Metal, accelerator backend, persistence, or multi-GPU code.
- Model footprint is reserved logically through ResourceManager exactly once per resident model.
- Eviction considers only resident models with ref_count == 0 and uses ascending last_loaded.
- A failed load must not evict or otherwise change existing residency.
- Repeated load of a resident model is idempotent and does not change last_loaded.
- Retain/release do not implicitly load or unload models.
- Existing v0.1–v0.3 wire behavior remains unchanged except routing model retain/release through the coordinator.
- Run focused tests after each red/green cycle and full CTest before completion.

## File Map

- Create include/gpumemd/model_residency.hpp: residency records, snapshots, and coordinator API.
- Create src/core/model_residency.cpp: locking, synthetic reservations, load/unload, LRU selection, and snapshots.
- Create tests/model_residency_test.cpp: core lifecycle, LRU, rollback, and concurrency tests.
- Modify include/gpumemd/model_registry.hpp and src/core/model_registry.cpp only if the shared error/result types require new residency error values.
- Modify CMakeLists.txt to compile the residency core and register its test.
- Modify include/gpumemd/protocol.hpp and src/protocol/protocol.cpp for load/unload/residency commands and exact formatting.
- Modify tests/protocol_test.cpp for parser and wire tests.
- Modify include/gpumemd/server.hpp, src/ipc/unix_socket_server.cpp, and src/daemon/main.cpp for coordinator wiring.
- Modify tests/server_test.cpp for residency IPC integration.
- Modify src/client/main.cpp, tests/cli_smoke.cmake, and README.md for public commands and v0.4 documentation.

---

### Task 1: Add the residency core with LRU eviction

**Files:**
- Create: include/gpumemd/model_residency.hpp
- Create: src/core/model_residency.cpp
- Create: tests/model_residency_test.cpp
- Modify: include/gpumemd/model_registry.hpp
- Modify: CMakeLists.txt

**Interfaces:**
- Extend ModelError with UnknownResidency and InsufficientMemory.
- ModelResidencyManager(ModelRegistry&, ResourceManager&).
- ModelOperationResult load(std::string_view id).
- ModelOperationResult unload(std::string_view id).
- ModelOperationResult retain(std::string_view id).
- ModelOperationResult release_model(std::string_view id).
- ResidencyRecord { std::string id; Bytes footprint_bytes; std::uint64_t ref_count; bool resident; std::uint64_t last_loaded; }.
- ResidencySnapshot { std::vector<ResidencyRecord> records; }.
- ResidencySnapshot residency() const.
- All methods are thread-safe; load/unload/retain/release_model serialize through the manager mutex.

- [ ] **Step 1: Write the failing tests**

Create tests/model_residency_test.cpp with real core tests:

~~~cpp
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
    registry.register_model("bert", 60, "base");
    assert(residency.load("bert").ok());
    assert(residency.retain("bert").amount == 1);
    assert(residency.unload("bert").error == ModelError::ModelInUse);
}

void evicts_oldest_unreferenced_model() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    registry.register_model("a", 40, "a");
    registry.register_model("b", 40, "b");
    registry.register_model("c", 40, "c");
    assert(residency.load("a").ok());
    assert(residency.load("b").ok());
    assert(residency.retain("b").ok());
    assert(residency.load("c").ok());
    const auto snapshot = residency.residency();
    assert(!snapshot.records[0].resident);
    assert(snapshot.records[1].resident);
    assert(snapshot.records[2].resident);
    assert(resources.status().used == 80);
}

void failed_load_rolls_back_evictions() {
    ModelRegistry registry;
    ResourceManager resources(100);
    ModelResidencyManager residency(registry, resources);
    registry.register_model("a", 60, "a");
    registry.register_model("b", 60, "b");
    residency.load("a");
    residency.retain("a");
    const auto result = residency.load("b");
    assert(result.error == ModelError::InsufficientMemory);
    assert(residency.residency().records[0].resident);
    assert(resources.status().used == 60);
}
~~~

Also test unknown/non-resident operations, oversized footprints, release underflow, monotonic load order, and concurrent retain/release on one registered model with an exact final refcount.

- [ ] **Step 2: Run tests to verify they fail**

Run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target model_residency_test

Expected: build fails because the residency API, implementation, target, and test do not exist.

- [ ] **Step 3: Write the minimal implementation**

Use a mutex-protected unordered_map of residency records keyed by model ID and a uint64_t next_loaded_. Obtain model metadata/footprint from a locked registry snapshot or add a focused lookup API. For a load, validate the model and footprint, collect evictable resident records sorted by last_loaded, and first compute the complete eviction plan. Only after confirming enough capacity can be made available, release synthetic labels model:<id>, acquire model:<new_id>, and update records. If any operation fails, restore the prior reservations and resident flags before returning. Route retain/release to the registry while holding the residency mutex.

- [ ] **Step 4: Run tests to verify they pass**

Run: cmake --build build --target model_residency_test && ./build/model_residency_test

Expected: PASS.

- [ ] **Step 5: Commit**

~~~sh
git add CMakeLists.txt include/gpumemd/model_registry.hpp include/gpumemd/model_residency.hpp src/core/model_residency.cpp tests/model_residency_test.cpp
git commit -m "feat: add simulated model residency"
~~~

---

### Task 2: Extend the wire protocol

**Files:**
- Modify: include/gpumemd/protocol.hpp
- Modify: src/protocol/protocol.cpp
- Modify: tests/protocol_test.cpp

**Interfaces:**
- Add CommandType values LoadModel, UnloadModel, Residency.
- Parse exactly load NAME, unload NAME, and residency.
- Add format_residency_operation_result and format_residency.
- Successful outputs are:
  OK loaded NAME FOOTPRINT
  OK unloaded NAME FOOTPRINT
  OK residency COUNT, RESIDENT rows, END.
- ModelError values UnknownResidency and InsufficientMemory serialize as unknown_residency and insufficient_memory.
- Preserve all existing v0.1–v0.3 parsing and formatting.

- [ ] **Step 1: Write the failing test**

Add exact parser and formatter assertions:

~~~cpp
void parses_residency_commands() {
    auto result = parse_command("load bert");
    assert(result.ok() && result.command.type == CommandType::LoadModel);
    result = parse_command("unload bert");
    assert(result.ok() && result.command.type == CommandType::UnloadModel);
    result = parse_command("residency");
    assert(result.ok() && result.command.type == CommandType::Residency);
    assert(parse_command("load bert extra").error == ParseError::InvalidRequest);
}

void formats_empty_and_loaded_residency() {
    assert(format_residency({}) == "OK residency 0\nEND\n");
    ResidencySnapshot snapshot{{{"bert", 7000000000, 0, true, 1}}};
    assert(format_residency(snapshot) ==
           "OK residency 1\nRESIDENT bert 7000000000 0 1\nEND\n");
}
~~~

- [ ] **Step 2: Run tests to verify they fail**

Run: cmake --build build --target protocol_test && ./build/protocol_test

Expected: compilation fails because the new command types and formatter declarations do not exist.

- [ ] **Step 3: Write the minimal implementation**

Add command types and parse branches using the existing name validator. Add model operation formatting for residency-specific success/error messages and snapshot formatting with deterministic rows and END terminator.

- [ ] **Step 4: Run tests to verify they pass**

Run: cmake --build build --target protocol_test && ./build/protocol_test

Expected: PASS.

- [ ] **Step 5: Commit**

~~~sh
git add include/gpumemd/protocol.hpp src/protocol/protocol.cpp tests/protocol_test.cpp
git commit -m "feat: add residency wire protocol"
~~~

---

### Task 3: Integrate residency into server and daemon

**Files:**
- Modify: include/gpumemd/server.hpp
- Modify: src/ipc/unix_socket_server.cpp
- Modify: src/daemon/main.cpp
- Modify: tests/server_test.cpp

**Interfaces:**
- UnixSocketServer accepts ResourceManager&, ModelRegistry&, ModelResidencyManager&, and socket path.
- Dispatch retain/release_model through ModelResidencyManager.
- Dispatch load, unload, and residency through ModelResidencyManager.
- Existing resource commands and v0.3 register/unregister/models remain behaviorally compatible.

- [ ] **Step 1: Write the failing test**

Add an integration test covering:

~~~cpp
assert(request(client, "register a 40 a") == "OK registered a 40\n");
assert(request(client, "register b 40 b") == "OK registered b 40\n");
assert(request(client, "register c 40 c") == "OK registered c 40\n");
assert(request(client, "load a") == "OK loaded a 40\n");
assert(request(client, "load b") == "OK loaded b 40\n");
assert(request(client, "retain b") == "OK retained b 1\n");
assert(request(client, "load c") == "OK loaded c 40\n");
assert(request(client, "residency") ==
       "OK residency 2\nRESIDENT b 40 1 2\nRESIDENT c 40 0 3\nEND\n");
assert(request(client, "status") ==
       "OK status 100 80 20 2\nCLIENT model:b 40\nCLIENT model:c 40\nEND\n");
~~~

- [ ] **Step 2: Run test to verify it fails**

Run: cmake --build build --target server_test && ./build/server_test

Expected: compilation fails because the server constructor and dispatch do not accept the residency manager.

- [ ] **Step 3: Write the minimal implementation**

Construct one ModelResidencyManager in daemon main after registry/resources. Pass it to the server. Extend dispatch with the three residency commands and route existing model reference operations through the coordinator. Keep shutdown and worker-thread behavior unchanged.

- [ ] **Step 4: Run test to verify it passes**

Run: cmake --build build --target server_test && ./build/server_test

Expected: PASS.

- [ ] **Step 5: Commit**

~~~sh
git add include/gpumemd/server.hpp src/ipc/unix_socket_server.cpp src/daemon/main.cpp tests/server_test.cpp
git commit -m "feat: integrate model residency with daemon"
~~~

---

### Task 4: Update CLI/docs and verify acceptance

**Files:**
- Modify: src/client/main.cpp
- Modify: tests/cli_smoke.cmake
- Modify: README.md
- Modify: CMakeLists.txt if a dedicated CLI scenario is added.

**Interfaces:**
- Help lists load NAME, unload NAME, and residency.
- README reports v0.4, explains explicit load/unload, LRU eviction, refcount protection, and simulated-only behavior.
- README states failed loads do not evict existing models and model footprints are logical reservations.

- [ ] **Step 1: Write the failing test**

Extend client-help assertions to require:

~~~text
load NAME
unload NAME
residency
~~~

- [ ] **Step 2: Run test to verify it fails**

Run: cmake --build build --target gpumemctl && ctest --test-dir build -R cli_client_help --output-on-failure

Expected: FAIL because v0.3 help does not list residency commands.

- [ ] **Step 3: Write the minimal implementation**

Update help and README with exact commands, examples, non-goals, and the v0.4 status. Do not add backend or real model-loading language beyond the documented simulation.

- [ ] **Step 4: Run test to verify it passes**

Run: cmake --build build --target gpumemctl && ctest --test-dir build -R cli_client_help --output-on-failure

Expected: PASS.

- [ ] **Step 5: Commit**

~~~sh
git add src/client/main.cpp tests/cli_smoke.cmake README.md CMakeLists.txt
git commit -m "docs: document v0.4 residency"
~~~

For final acceptance, run:

~~~sh
cmake -S . -B build-v04-final -DCMAKE_BUILD_TYPE=Debug
cmake --build build-v04-final --parallel
ctest --test-dir build-v04-final --output-on-failure
~~~

Expected: all tests pass. Start a 16 GB daemon, run concurrent processA/processB acquisitions, register/load three models, retain model b, load model c to evict older unreferenced model a, inspect residency and resource status, unload/release/unregister all models, and verify final status reports used 0 and free 16000000000.

