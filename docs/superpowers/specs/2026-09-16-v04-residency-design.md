# gpumemd v0.4 Model Residency Design

## Goal

Add simulated model residency with explicit load/unload operations and
reference-aware LRU eviction, while keeping model metadata, logical resource
accounting, and future accelerator backends separate.

## Scope

v0.4 simulates residency only. `load` reserves a model's declared footprint
from the existing `ResourceManager`; no model bytes are allocated and no
CUDA/Metal API is used. `unload` releases that logical reservation. When a
load needs more free capacity, the residency manager evicts resident models
with `ref_count == 0` in least-recently-loaded order until the request fits.

This release does not add model loading, model data handles, accelerator
backends, persistence, multi-GPU placement, or LFU/cost-aware policies.

## Architecture

```text
gpumemctl -> UnixSocketServer -> ModelResidencyManager
                                  |       |       |
                                  v       v       v
                           ModelRegistry  ResourceManager  LRU state
```

`ModelRegistry` remains the source of model identity, metadata, footprint, and
reference count. `ResourceManager` remains the source of simulated capacity
and reservations. `ModelResidencyManager` coordinates both behind its own
mutex and owns only residency state and load-order information. The server
routes model lifecycle operations through the residency manager so retain,
release, load, unload, and eviction share one coordination boundary.

Internal resource reservations use the private owner label `model:<id>`.
Because model IDs cannot contain `:`, this cannot collide with valid client
names. These labels are an implementation detail; the public model output
reports model IDs rather than resource-owner labels.

## Residency state and API

Each resident record contains:

- `id`
- `footprint_bytes`
- `ref_count`
- `resident`
- `last_loaded`

`last_loaded` is a monotonic `uint64_t` load-order sequence owned by the
residency manager. It increments on every successful transition from
non-resident to resident. Repeated `load` on an already resident model is
idempotent and does not consume capacity or change `last_loaded`.

The manager exposes:

```cpp
ModelOperationResult load(std::string_view id);
ModelOperationResult unload(std::string_view id);
ModelOperationResult retain(std::string_view id);
ModelOperationResult release_model(std::string_view id);
ResidencySnapshot residency() const;
```

`load` rejects an unknown model and a model whose footprint exceeds total
capacity. It first selects evictable resident models (`ref_count == 0`), sorted
by ascending `last_loaded`, and releases their logical reservations until the
requested footprint fits. If capacity still cannot fit, it returns
`insufficient_memory` and leaves all prior residency unchanged.

`unload` rejects unknown and non-resident models. It returns `model_in_use` if
the model has a positive reference count; otherwise it releases the logical
reservation and removes the residency record. `retain` and `release_model`
preserve the v0.3 reference-count rules and work for resident or non-resident
models. Releasing the last reference does not unload a model automatically.

## IPC protocol

Requests remain one newline-terminated ASCII command per line:

```text
load bert
unload bert
residency
```

Successful responses are:

```text
OK loaded bert 7000000000
OK unloaded bert 7000000000
OK residency 1
RESIDENT bert 7000000000 0 1
END
```

The load/unload amount is the model footprint. `residency` rows contain
`id`, `footprint_bytes`, `ref_count`, and `last_loaded`, and are sorted by ID.
An empty snapshot is `OK residency 0` followed by `END`.

Dedicated errors are `unknown_model`, `unknown_residency`, `model_in_use`,
`insufficient_memory`, and existing validation errors. An unsuccessful load
must not evict any model. Existing v0.1–v0.3 commands retain their wire
behavior, except model `retain` and `release_model` are routed through the
coordinator.

## Testing strategy

Core tests cover loading, idempotent load, unload, unknown/non-resident models,
unload protection while referenced, capacity reservation, oversized models,
LRU eviction, refusal to evict referenced models, rollback when a load cannot
fit, and concurrent retain/release/load operations. Protocol tests cover new
commands, exact success/error formatting, malformed arity, and deterministic
empty/non-empty snapshots. Server tests cover the complete residency lifecycle
and verify that resource status reflects model reservations.

The acceptance test starts a 16 GB daemon, registers three models, loads two,
references one, loads a third to force eviction of the older unreferenced
model, verifies the referenced model is retained, then unloads/releases all
models and confirms resource capacity returns to 16 GB.

## Future compatibility decisions

The residency manager exposes logical transitions rather than device handles or
model pointers. The private reservation-label convention can later be replaced
by a typed resource-owner interface without changing the public model protocol.
The explicit coordinator boundary also leaves room for a future backend to
perform actual load/unload work after the simulated accounting decisions.
