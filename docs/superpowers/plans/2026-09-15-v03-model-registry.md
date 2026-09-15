# v0.3 Model Registry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a thread-safe, hardware-independent model registry and Unix-socket protocol while fixing the impossible-request edge case in v0.2.

**Architecture:** Keep ResourceManager and the new ModelRegistry as independent core components, each protected by its own mutex and exposing copied snapshots. Extend the existing parser and server dispatch layer to route model commands without coupling model metadata to simulated memory reservations.

**Tech Stack:** C++20, CMake 3.20+, POSIX Unix domain sockets, CTest, standard library only.

**Spec:** docs/superpowers/specs/2026-09-15-v03-model-registry-design.md

## Global Constraints

- No CUDA, Metal, model loading, eviction, residency allocation, or third-party dependencies.
- Model footprint is descriptive only and must not change ResourceManager capacity.
- Model IDs use the existing 1–64 ASCII name rule.
- Metadata is one non-empty ASCII token, 1–128 bytes, without whitespace or newline.
- last_access is a monotonic logical uint64_t sequence, not wall-clock time.
- Preserve existing v0.1/v0.2 commands and response behavior.
- Run the smallest relevant test after each red/green cycle and the full CTest suite before completion.

## File Map

- Create include/gpumemd/model_registry.hpp: model records, errors, results, snapshots, and thread-safe registry API.
- Create src/core/model_registry.cpp: registry validation, mutations, locking, and deterministic snapshots.
- Create tests/model_registry_test.cpp: registry unit and concurrency tests.
- Modify include/gpumemd/protocol.hpp and src/protocol/protocol.cpp: model command types, parsing, and formatting.
- Modify include/gpumemd/server.hpp and src/ipc/unix_socket_server.cpp: registry wiring and dispatch.
- Modify src/daemon/main.cpp: construct the registry.
- Modify src/client/main.cpp: show v0.3 command syntax.
- Modify CMakeLists.txt: compile/link the registry and register tests.
- Modify tests/resource_manager_test.cpp, tests/protocol_test.cpp, and tests/server_test.cpp for regression and v0.3 coverage.
- Modify README.md: document v0.3 commands and current status.

---

### Task 1: Fix impossible v0.2 acquire requests

**Files:**
- Modify: src/core/resource_manager.cpp
- Test: tests/resource_manager_test.cpp

**Interfaces:**
- Keep ResourceManager::acquire(std::string_view, Bytes, AcquireOptions) unchanged.
- A request with bytes > capacity_ returns InsufficientMemory before entering pending_.

- [ ] **Step 1: Write the failing test**

Add oversized_infinite_acquire_fails_immediately:

~~~cpp
void oversized_infinite_acquire_fails_immediately() {
    ResourceManager manager(100);
    const auto start = std::chrono::steady_clock::now();
    const auto result = manager.acquire("impossible", 101);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(!result.ok() && result.error == ErrorCode::InsufficientMemory);
    assert(elapsed < 200ms);
    assert(manager.status().used == 0);
}
~~~

Call it from main.

- [ ] **Step 2: Run the test to verify it fails**

Run: cmake --build build --target resource_manager_test && timeout 1 ./build/resource_manager_test

Expected: the test times out or fails because the request waits forever.

- [ ] **Step 3: Write the minimal implementation**

After validating timeout and before the timeout == 0 branch, add:

~~~cpp
if (bytes > capacity_) {
    return {ErrorCode::InsufficientMemory, 0};
}
~~~

- [ ] **Step 4: Run the test to verify it passes**

Run: cmake --build build --target resource_manager_test && ./build/resource_manager_test

Expected: PASS and prompt exit.

- [ ] **Step 5: Commit**

~~~sh
git add src/core/resource_manager.cpp tests/resource_manager_test.cpp
git commit -m "fix: reject impossible acquire requests"
~~~

---

### Task 2: Add the model registry core API and behavior

**Files:**
- Create: include/gpumemd/model_registry.hpp
- Create: src/core/model_registry.cpp
- Modify: CMakeLists.txt
- Test: tests/model_registry_test.cpp

**Interfaces:**
- ModelError { None, InvalidId, InvalidMetadata, InvalidSize, DuplicateModel, UnknownModel, ModelInUse, RefcountUnderflow }.
- ModelOperationResult { ModelError error; Bytes amount; bool ok() const noexcept; }.
- ModelRecord { std::string id; std::string metadata; Bytes footprint_bytes; uint64_t ref_count; uint64_t last_access; }.
- ModelSnapshot { std::vector<ModelRecord> models; }.
- ModelRegistry with register_model, unregister_model, retain, release_model, and models.
- All methods are thread-safe. register_model starts ref_count and last_access at zero; successful retain/release increments a private sequence and writes it to the record.
- release_model with zero references returns RefcountUnderflow; it never underflows.
- Snapshot records are sorted by ID.

- [ ] **Step 1: Write the failing tests**

Create tests/model_registry_test.cpp covering registration, duplicate/invalid input, lifecycle, underflow, monotonic access sequence, deterministic snapshots, and concurrent unique registrations:

~~~cpp
void registers_model_with_initial_state() {
    ModelRegistry registry;
    const auto result = registry.register_model("bert", 7000000000, "bert-base");
    assert(result.ok() && result.amount == 7000000000);
    const auto snapshot = registry.models();
    assert(snapshot.models.size() == 1);
    assert(snapshot.models[0].id == "bert");
    assert(snapshot.models[0].metadata == "bert-base");
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

void access_sequence_is_monotonic() {
    ModelRegistry registry;
    registry.register_model("a", 1, "a");
    registry.register_model("b", 1, "b");
    registry.retain("a");
    const auto first = registry.models().models[0].last_access;
    registry.retain("b");
    const auto second = registry.models().models[1].last_access;
    registry.release_model("b");
    const auto third = registry.models().models[1].last_access;
    assert(first > 0 && first < second && second < third);
}
~~~

Add a concurrent test with multiple threads registering unique IDs and assert every operation succeeds and snapshot size equals the thread count.

- [ ] **Step 2: Run the test to verify it fails**

Run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target model_registry_test

Expected: build fails because the registry files and target do not exist.

- [ ] **Step 3: Write the minimal implementation**

Use an unordered_map<string, ModelRecord>, uint64_t next_access_, and mutable mutex. Reuse the ResourceManager ASCII ID rule. Validate metadata as 1–128 ASCII bytes excluding whitespace, controls, and newline. Lock every mutation and snapshot copy. Add the implementation to gpumemd_core and link the unit test.

- [ ] **Step 4: Run the test to verify it passes**

Run: cmake --build build --target model_registry_test && ./build/model_registry_test

Expected: PASS with exit code 0.

- [ ] **Step 5: Commit**

~~~sh
git add CMakeLists.txt include/gpumemd/model_registry.hpp src/core/model_registry.cpp tests/model_registry_test.cpp
git commit -m "feat: add thread-safe model registry"
~~~

---

### Task 3: Extend protocol parsing and formatting

**Files:**
- Modify: include/gpumemd/protocol.hpp
- Modify: src/protocol/protocol.cpp
- Modify: tests/protocol_test.cpp

**Interfaces:**
- Extend CommandType with RegisterModel, UnregisterModel, RetainModel, ReleaseModel, Models.
- Extend ParseError with InvalidModelId and InvalidMetadata so parser-level model errors use the dedicated wire names.
- Extend Command with std::string metadata.
- Add format_model_operation_result and format_models.
- Parse register ID SIZE METADATA, unregister ID, retain ID, release_model ID, and models.
- Reject wrong arity and invalid ID/metadata/size before dispatch.

- [ ] **Step 1: Write the failing tests**

Add parser assertions:

~~~cpp
void parses_model_commands() {
    auto result = parse_command("register bert 7GB bert-base");
    assert(result.ok() && result.command.type == CommandType::RegisterModel);
    assert(result.command.name == "bert");
    assert(result.command.bytes == 7000000000);
    assert(result.command.metadata == "bert-base");
    result = parse_command("unregister bert");
    assert(result.ok() && result.command.type == CommandType::UnregisterModel);
    result = parse_command("retain bert");
    assert(result.ok() && result.command.type == CommandType::RetainModel);
    result = parse_command("release_model bert");
    assert(result.ok() && result.command.type == CommandType::ReleaseModel);
    result = parse_command("models");
    assert(result.ok() && result.command.type == CommandType::Models);
}

void rejects_invalid_model_commands() {
    assert(parse_command("register bert 1").error == ParseError::InvalidRequest);
    assert(parse_command("register bad/id 1 base").error == ParseError::InvalidName);
    assert(parse_command("register bert 0 base").error == ParseError::InvalidSize);
    assert(parse_command("register bert 1 bad metadata").error == ParseError::InvalidRequest);
    assert(parse_command("retain missing extra").error == ParseError::InvalidRequest);
}
~~~

Add exact formatting assertions for model success/errors and the models response.

- [ ] **Step 2: Run the test to verify it fails**

Run: cmake --build build --target protocol_test && ./build/protocol_test

Expected: compilation fails because model command types and fields are absent.

- [ ] **Step 3: Write the minimal implementation**

Add command types and metadata. Reuse size and ID parsing helpers. Add a metadata validator with the stated length and ASCII rules. Serialize InvalidModelId and InvalidMetadata with their dedicated wire names, then serialize stable model operation errors and deterministic snapshots while preserving existing v0.1/v0.2 formatting.

- [ ] **Step 4: Run the test to verify it passes**

Run: cmake --build build --target protocol_test && ./build/protocol_test

Expected: PASS.

- [ ] **Step 5: Commit**

~~~sh
git add include/gpumemd/protocol.hpp src/protocol/protocol.cpp tests/protocol_test.cpp
git commit -m "feat: add v0.3 model protocol"
~~~

---

### Task 4: Wire the registry into the Unix server and daemon

**Files:**
- Modify: include/gpumemd/server.hpp
- Modify: src/ipc/unix_socket_server.cpp
- Modify: src/daemon/main.cpp
- Modify: tests/server_test.cpp

**Interfaces:**
- UnixSocketServer accepts ResourceManager&, ModelRegistry&, and socket path.
- Dispatch model commands to the corresponding registry method and formatter.
- Existing resource commands remain unchanged.
- The daemon constructs one registry for its lifetime.

- [ ] **Step 1: Write the failing integration test**

Add a temporary-socket test:

~~~cpp
assert(request(client, "register bert 7GB bert-base") ==
       "OK registered bert 7000000000\n");
assert(request(client, "retain bert") == "OK retained bert 1\n");
assert(request(client, "models") ==
       "OK models 1\nMODEL bert bert-base 7000000000 1 1\nEND\n");
assert(request(client, "release_model bert") == "OK released_model bert 0\n");
assert(request(client, "unregister bert") == "OK unregistered bert 0\n");
~~~

Also assert that registering a large model footprint does not alter resource status.

- [ ] **Step 2: Run the test to verify it fails**

Run: cmake --build build --target server_test && ./build/server_test

Expected: compilation fails because the server has no ModelRegistry dependency.

- [ ] **Step 3: Write the minimal implementation**

Add a ModelRegistry reference member and constructor parameter. Extend dispatch with the five model command cases. Keep the existing worker and shutdown behavior unchanged. Construct the registry in daemon main and pass it to the server.

- [ ] **Step 4: Run the test to verify it passes**

Run: cmake --build build --target server_test && ./build/server_test

Expected: PASS.

- [ ] **Step 5: Commit**

~~~sh
git add include/gpumemd/server.hpp src/ipc/unix_socket_server.cpp src/daemon/main.cpp tests/server_test.cpp
git commit -m "feat: serve model registry over Unix socket"
~~~

---

### Task 5: Update CLI and documentation

**Files:**
- Modify: src/client/main.cpp
- Modify: README.md
- Modify: tests/cli_smoke.cmake if help assertions require it.

**Interfaces:**
- gpumemctl help lists the five model commands and arguments.
- README reports v0.3 implemented and states that footprint is descriptive only.
- README documents the v0.2 impossible-request behavior and timeout=0.

- [ ] **Step 1: Write the failing test**

Extend the client help smoke assertion to require:

~~~text
register NAME SIZE METADATA
unregister NAME
retain NAME
release_model NAME
models
~~~

- [ ] **Step 2: Run the test to verify it fails**

Run: cmake --build build --target gpumemctl && ctest --test-dir build -R cli_client_help --output-on-failure

Expected: FAIL because current help lists only resource commands.

- [ ] **Step 3: Write the minimal implementation**

Update client usage with the exact command forms. Update README examples, current milestone, registry semantics, and non-goals. Do not add future subsystem code.

- [ ] **Step 4: Run the test to verify it passes**

Run: cmake --build build --target gpumemctl && ctest --test-dir build -R cli_client_help --output-on-failure

Expected: PASS.

- [ ] **Step 5: Commit**

~~~sh
git add src/client/main.cpp README.md tests/cli_smoke.cmake
git commit -m "docs: document v0.3 model registry"
~~~

---

### Task 6: Full verification and live acceptance

**Files:**
- Verify: all source, tests, and documentation.
- Modify: none unless verification exposes a defect.

- [ ] **Step 1: Configure and build**

Run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel

Expected: exit code 0 with all targets built.

- [ ] **Step 2: Run the complete test suite**

Run: ctest --test-dir build --output-on-failure

Expected: all tests pass.

- [ ] **Step 3: Run the daemon acceptance flow**

Start the daemon with a fresh socket path and 16GB capacity. In concurrent clients, acquire processA 4GB and processB 8GB; register and retain a model; query status and models; release both resources; release the model and unregister it. Verify final resource output has used 0 and free 16000000000, and final model output has no rows after unregistering.

- [ ] **Step 4: Inspect repository state**

Run: git status --short && git log --oneline --decorate -8

Expected: only intentional changes are present and implementation commits are visible.

- [ ] **Step 5: Commit only if needed**

Do not create a final commit if Tasks 1–5 already produced clean commits. If verification-only fixes were necessary, commit them with a focused message.
