# Task 4 Report: Update CLI/docs and verify acceptance

## Status

Complete. The v0.4 client help, smoke coverage, and README now describe the
implemented model-residency interface and its simulation boundaries.

## Implementation

- Extended `gpumemctl --help` with `load NAME`, `unload NAME`, and `residency`.
- Extended the existing client-help smoke scenario to require all three
  residency commands. No CMake change was needed.
- Updated the README from v0.3 to v0.4 and documented:
  - explicit load/unload behavior;
  - least-recently-loaded eviction of unreferenced residents;
  - refcount protection from eviction and explicit unload;
  - simulation-only behavior with no accelerator or model-file loading;
  - model footprints as logical reservations in shared broker capacity; and
  - preservation of existing residency and reservations after failed loads.

## TDD evidence

### RED

After extending `tests/cli_smoke.cmake` first, the prescribed focused command
failed because the existing help did not contain `load NAME`:

```sh
cmake --build build --target gpumemctl && \
ctest --test-dir build -R cli_client_help --output-on-failure
```

The failure was `client_help: missing 'load NAME'`, proving that the test
observed the public help behavior before implementation.

### GREEN

After the minimal client-help update, the same command passed 1/1 tests.

## Final verification

The prescribed fresh acceptance build completed successfully:

```sh
cmake -S . -B build-v04-final -DCMAKE_BUILD_TYPE=Debug
cmake --build build-v04-final --parallel
ctest --test-dir build-v04-final --output-on-failure
```

Result: 10/10 tests passed, 0 failed.

The live 16 GB acceptance scenario also passed:

- Concurrent `processA` and `processB` acquisitions reserved 2 GB each.
- Models `a`, `b`, and `c` were registered with 6 GB footprints.
- Loading `a` and `b`, retaining `b`, and loading `c` evicted only `a`.
- Residency showed `b` and `c`; status showed all 16 GB logically reserved.
- Releasing `b`, unloading `b` and `c`, releasing both clients, and
  unregistering all models left residency empty.
- Final status was `OK status 16000000000 0 16000000000 0`.

`git diff --check` passed before commit.

## Commit

- `docs: document v0.4 residency`

## Concerns

- Initial runs of freshly generated macOS binaries were transiently killed by
  the environment, as observed in Task 3. Direct launches and subsequent fresh
  full-suite execution passed; no product change was made for runner behavior.
- Acceptance artifacts remain under `/tmp/gpumemd-v04-acceptance-M6u7ss`
  because the command runner rejected automated temporary-directory removal.

## Review corrections

- Made the documented command sequence release `processA`, `processB`, and
  `processC` before loading the 7 GB `bert` model. This restores all 16 GB, so
  the example is runnable in order while preserving the explicit
  register/load/retain/release/unload/unregister lifecycle.
- Strengthened the client-help smoke test to match `load NAME`, `unload NAME`,
  and `residency` as complete output lines. `unload NAME` can no longer satisfy
  the independent `load NAME` requirement through substring matching.

### Review RED/GREEN evidence

With only the complete-line assertions added, the focused test failed at the
new assertion:

```text
client_help: missing complete line ' load NAME'
```

After rendering each residency command on its own help line, the focused test
passed 1/1:

```sh
cmake --build build --target gpumemctl && \
ctest --test-dir build -R cli_client_help --output-on-failure
```

The rebuilt `build-v04-final` tree passed 10/10 tests after removing the
`com.apple.provenance` extended attribute from generated executables, which
stopped the environment's transient macOS process kills. Source files and
tracked build configuration were not changed by this stabilization step.

The README commands were then run in their documented order against a fresh
16 GB daemon. All resource acquisitions and releases succeeded, `load bert`
returned `OK loaded bert 7000000000`, and the complete model lifecycle ended
with:

```text
OK status 16000000000 0 16000000000 0
END
```
