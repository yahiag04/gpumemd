#include "gpumemd/memory_allocator.hpp"

#include <algorithm>
#include <stdexcept>

namespace gpumemd {

MemoryAllocator::MemoryAllocator(Bytes capacity, AllocationStrategy strategy)
    : capacity_(capacity), strategy_(strategy), free_blocks_{{0, capacity}} {
    if (capacity == 0) {
        throw std::invalid_argument("allocator capacity must be positive");
    }
}

bool MemoryAllocator::valid_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_' ||
               character == '-' || character == '.';
    });
}

AllocationResult MemoryAllocator::allocate(std::string_view id, Bytes bytes) {
    if (!valid_id(id)) {
        return {AllocatorError::InvalidId, 0, 0};
    }
    if (bytes == 0) {
        return {AllocatorError::InvalidSize, 0, 0};
    }

    std::lock_guard lock(mutex_);
    if (std::any_of(allocations_.begin(), allocations_.end(),
                    [id](const auto& allocation) { return allocation.id == id; })) {
        return {AllocatorError::DuplicateAllocation, 0, 0};
    }

    Bytes offset = 0;
    if (strategy_ == AllocationStrategy::Direct) {
        if (bytes > capacity_ - used_) {
            return {AllocatorError::OutOfMemory, 0, 0};
        }
        offset = direct_next_offset_;
        direct_next_offset_ += bytes;
    } else {
        const auto block = std::find_if(
            free_blocks_.begin(), free_blocks_.end(),
            [bytes](const auto& candidate) { return candidate.bytes >= bytes; });
        if (block == free_blocks_.end()) {
            return {AllocatorError::OutOfMemory, 0, 0};
        }
        offset = block->offset;
        block->offset += bytes;
        block->bytes -= bytes;
        if (block->bytes == 0) {
            free_blocks_.erase(block);
        }
    }

    allocations_.push_back({std::string(id), offset, bytes});
    used_ += bytes;
    return {AllocatorError::None, offset, bytes};
}

AllocationResult MemoryAllocator::release(std::string_view id) {
    std::lock_guard lock(mutex_);
    const auto iterator = std::find_if(
        allocations_.begin(), allocations_.end(),
        [id](const auto& allocation) { return allocation.id == id; });
    if (iterator == allocations_.end()) {
        return {AllocatorError::UnknownAllocation, 0, 0};
    }
    const auto result = AllocationResult{AllocatorError::None, iterator->offset, iterator->bytes};
    if (strategy_ == AllocationStrategy::Pool) {
        free_blocks_.push_back({iterator->offset, iterator->bytes});
        coalesce_locked();
    }
    used_ -= iterator->bytes;
    allocations_.erase(iterator);
    return result;
}

void MemoryAllocator::coalesce_locked() {
    std::sort(free_blocks_.begin(), free_blocks_.end(),
              [](const auto& left, const auto& right) { return left.offset < right.offset; });
    std::vector<FreeBlock> merged;
    for (const auto& block : free_blocks_) {
        if (!merged.empty() && merged.back().offset + merged.back().bytes == block.offset) {
            merged.back().bytes += block.bytes;
        } else {
            merged.push_back(block);
        }
    }
    free_blocks_ = std::move(merged);
}

AllocatorSnapshot MemoryAllocator::snapshot() const {
    std::lock_guard lock(mutex_);
    AllocatorSnapshot snapshot{capacity_, used_, capacity_ - used_, allocations_};
    std::sort(snapshot.allocations.begin(), snapshot.allocations.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    return snapshot;
}

} // namespace gpumemd
