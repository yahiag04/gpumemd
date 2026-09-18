#pragma once

#include "gpumemd/memory.hpp"

#include <cstdint>
#include <mutex>

namespace gpumemd {

struct MetricsSnapshot {
    std::uint64_t requests{0}, successes{0}, failures{0};
    std::uint64_t acquires{0}, releases{0}, model_loads{0}, model_unloads{0};
    Bytes acquired_bytes{0}, released_bytes{0};
};

class Metrics final {
public:
    void record_request(bool success);
    void record_acquire(bool success, Bytes bytes = 0);
    void record_release(bool success, Bytes bytes = 0);
    void record_model_load(bool success);
    void record_model_unload(bool success);
    [[nodiscard]] MetricsSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    MetricsSnapshot snapshot_;
};

} // namespace gpumemd
