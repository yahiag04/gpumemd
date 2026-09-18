#include "gpumemd/metrics.hpp"

namespace gpumemd {
void Metrics::record_request(bool success) { std::lock_guard lock(mutex_); ++snapshot_.requests; success ? ++snapshot_.successes : ++snapshot_.failures; }
void Metrics::record_acquire(bool success, Bytes bytes) { std::lock_guard lock(mutex_); ++snapshot_.acquires; if (success) snapshot_.acquired_bytes += bytes; }
void Metrics::record_release(bool success, Bytes bytes) { std::lock_guard lock(mutex_); ++snapshot_.releases; if (success) snapshot_.released_bytes += bytes; }
void Metrics::record_model_load(bool success) { std::lock_guard lock(mutex_); if (success) ++snapshot_.model_loads; }
void Metrics::record_model_unload(bool success) { std::lock_guard lock(mutex_); if (success) ++snapshot_.model_unloads; }
MetricsSnapshot Metrics::snapshot() const { std::lock_guard lock(mutex_); return snapshot_; }
} // namespace gpumemd
