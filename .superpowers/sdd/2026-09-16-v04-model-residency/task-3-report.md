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

## TDD evidence

- RED: `cmake --build build --target server_test` failed because the existing server constructor accepted three arguments instead of the required four.
- GREEN: `cmake --build build --target server_test && ./build/server_test` completed with exit code 0.

## Verification

- Full build: `cmake --build build` completed successfully.
- Full suite: `ctest --test-dir build --output-on-failure -j1` passed 10/10 tests.
- `git diff --check` passed.

The first full CTest invocation reported several unrelated processes as `Subprocess killed`; each affected binary passed when run directly, and a fresh sequential full-suite run passed 10/10. No code change was made for the transient runner behavior.

## Concerns

None in Task 3 scope.
