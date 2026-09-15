# gpumemd v0.3 Model Registry Design

## Goal

Add a thread-safe, hardware-independent model registry that tracks model
metadata, declared memory footprint, reference count, and monotonic last-access
order, while preserving the v0.2 resource broker as a separate subsystem.

## Scope

This release includes only registry bookkeeping and its Unix-socket protocol.
The registry does not allocate memory, reserve broker capacity, load models,
evict models, or communicate with CUDA/Metal. A model's declared footprint is
descriptive metadata and is not automatically passed to `ResourceManager`.

The v0.2 correctness fix is included in the release: an acquire request larger
than total capacity returns `insufficient_memory` immediately, including when
its timeout is infinite. A zero timeout remains an immediate, non-blocking
attempt.

## Architecture

```text
gpumemctl -> UnixSocketServer -> protocol dispatch
                                  |             |
                                  v             v
                         ResourceManager   ModelRegistry
```

`ResourceManager` continues to own simulated capacity and reservations.
`ModelRegistry` owns only model records. Each component has its own mutex and
returns copied snapshots; neither component depends on sockets, formatting, or
accelerator APIs. The server is the sole composition and dispatch layer.

## Model data and API

Each record contains:

- `id`: 1–64 ASCII letters, digits, `_`, `-`, or `.`; unique in the registry.
- `metadata`: one non-empty ASCII token, 1–128 bytes, with no whitespace or
  newline. v0.3 deliberately does not add quoting or escaping.
- `footprint_bytes`: positive `uint64_t` byte count.
- `ref_count`: unsigned count, initially zero.
- `last_access`: monotonic `uint64_t` sequence, initially zero. The registry
  increments the sequence on every successful `retain` and `release_model`.
  This is an ordering key, not wall-clock time, and is suitable for a future
  LRU policy.

The registry exposes:

```cpp
OperationResult register_model(std::string_view id, Bytes footprint,
                               std::string_view metadata);
OperationResult unregister_model(std::string_view id);
OperationResult retain(std::string_view id);
OperationResult release_model(std::string_view id);
ModelSnapshot models() const;
```

`register_model` rejects duplicate IDs and invalid fields. `unregister_model`
rejects unknown IDs and records with a non-zero reference count. `retain`
rejects unknown IDs and increments the count. `release_model` rejects unknown
IDs and zero counts, otherwise decrements the count. Records remain registered
when their count reaches zero.

## IPC protocol

Requests are one newline-terminated ASCII command per line, using the existing
4096-byte line limit and whitespace tokenizer.

```text
register MODEL 7GB bert-base
unregister MODEL
retain MODEL
release_model MODEL
models
```

Successful mutation responses are:

```text
OK registered MODEL 7000000000
OK unregistered MODEL 0
OK retained MODEL 1
OK released_model MODEL 0
```

The numeric mutation value is the footprint for `register` and the resulting
reference count for the other operations. `models` returns a deterministic
snapshot sorted by model ID:

```text
OK models 1
MODEL MODEL bert-base 7000000000 0 2
END
```

The row fields are `id`, `metadata`, `footprint_bytes`, `ref_count`, and
`last_access`. An empty registry returns `OK models 0` followed by `END`.

Dedicated wire errors are `duplicate_model`, `unknown_model`,
`model_in_use`, `invalid_model_id`, `invalid_metadata`, and `invalid_size`,
with human-readable messages and no embedded newlines. Existing v0.1/v0.2
errors and commands retain their behavior.

## Testing strategy

Core unit tests cover registration, duplicate and invalid input rejection,
unregister rules, retain/release transitions, underflow protection, monotonic
access ordering, deterministic snapshots, and concurrent operations. Protocol
unit tests cover all new commands, exact response formatting, malformed
argument counts, invalid metadata, and error formatting. Server integration
tests cover registry commands over Unix sockets, interaction with existing
resource commands, multiple clients, and clean shutdown.

The full CTest suite must pass after each implementation increment. The live
acceptance check starts a 16 GB daemon, exercises concurrent resource clients,
registers and references a model, then verifies both resource and model
snapshots.

## Future compatibility decisions

The registry never treats a model ID as an OS process identity and never puts
device handles on the wire. Footprint remains a logical byte count. Keeping
model bookkeeping separate from simulated reservations leaves room for a
future residency layer to coordinate loading and eviction without coupling
the registry to CUDA contexts, pointers, or socket lifetimes.
