#pragma once

#include "gpumemd/memory.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace gpumemd {

enum class AllocationStrategy { Direct, Pool };

enum class AllocatorError {
    None,
    InvalidId,
    InvalidSize,
    DuplicateAllocation,
    UnknownAllocation,
    OutOfMemory,
};

struct AllocationResult {
    AllocatorError error{AllocatorError::None};
    Bytes offset{0};
    Bytes bytes{0};

    [[nodiscard]] bool ok() const noexcept { return error == AllocatorError::None; }
};

struct AllocationRecord {
    std::string id;
    Bytes offset{0};
    Bytes bytes{0};
};

struct AllocatorSnapshot {
    Bytes capacity{0};
    Bytes used{0};
    Bytes free{0};
    std::vector<AllocationRecord> allocations;
};

class MemoryAllocator final {
public:
    explicit MemoryAllocator(Bytes capacity,
                             AllocationStrategy strategy = AllocationStrategy::Pool);

    [[nodiscard]] AllocationResult allocate(std::string_view id, Bytes bytes);
    [[nodiscard]] AllocationResult release(std::string_view id);
    [[nodiscard]] AllocatorSnapshot snapshot() const;

private:
    struct FreeBlock {
        Bytes offset{0};
        Bytes bytes{0};
    };

    static bool valid_id(std::string_view id) noexcept;
    void coalesce_locked();

    const Bytes capacity_;
    const AllocationStrategy strategy_;
    Bytes used_{0};
    Bytes direct_next_offset_{0};
    std::vector<FreeBlock> free_blocks_;
    std::vector<AllocationRecord> allocations_;
    mutable std::mutex mutex_;
};

} // namespace gpumemd
