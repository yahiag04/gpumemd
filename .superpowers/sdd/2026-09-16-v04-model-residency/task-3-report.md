# Task 3 Report: Integrate residency into server and daemon

## Status

Complete.

## Implementation

- Added `ModelResidencyManager&` to `UnixSocketServer` construction and state.
- Routed `retain` and `release_model` through the residency manager.
- Added server dispatch for `load`, `unload`, and `residency`.
- Constructed one residency manager in the daemon and injected it into the server.
- Preserved resource commands and registry commands; registration alone still leaves resource status unchanged.
- Added IPC integration coverage for eviction, retained residency, residency formatting, and model-backed resource accounting.

## Review fixes

- Restored v0.3 compatibility for references to registered, nonresident models: `retain` and `release_model` update the registry refcount and maintain a cold residency record without allocating memory.
- Kept loading explicit and retained `UnknownResidency` for unloading a cold model.
- Added serialized `ModelResidencyManager::unregister_model`: resident models return `ModelInUse`; cold models delegate registry refcount validation and erase their residency record only after successful unregister.
- Routed server `unregister` through the residency coordinator.
- Restored the original IPC lifecycle `register -> retain -> release_model -> unregister` and added IPC coverage proving a loaded model cannot be unregistered or orphan its reservation.

## TDD evidence

- RED: `cmake --build build --target server_test` failed because the existing server constructor accepted three arguments instead of the required four.
- GREEN: `cmake --build build --target server_test && ./build/server_test` completed with exit code 0.
- Review RED: focused test compilation failed because `ModelResidencyManager::unregister_model` did not exist; the new compatibility assertions also captured the prior cold-reference rejection.
- Review GREEN: `cmake --build build --target model_residency_test server_test && ./build/model_residency_test && ./build/server_test` completed with exit code 0.

## Verification

- Full build: `cmake --build build` completed successfully.
- Full suite: `ctest --test-dir build --output-on-failure -j1` passed 10/10 tests.
- `git diff --check` passed.

Some initial full CTest invocations reported unrelated processes as `Subprocess killed` or timed out; each affected test passed in isolation, and fresh sequential full-suite runs passed 10/10. No code change was made for the transient runner behavior.

## Concerns

None in Task 3 scope.
