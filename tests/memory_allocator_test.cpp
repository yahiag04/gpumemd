#include "gpumemd/memory_allocator.hpp"

#include <cassert>

int main() {
    gpumemd::MemoryAllocator allocator(100);
    assert(allocator.allocate("a", 40).offset == 0);
    assert(allocator.allocate("b", 40).offset == 40);
    assert(allocator.release("a").ok());
    assert(allocator.allocate("c", 30).offset == 0);
    assert(allocator.release("b").ok());
    assert(allocator.release("c").ok());
    const auto snapshot = allocator.snapshot();
    assert(snapshot.used == 0);
    assert(snapshot.free == 100);
    assert(allocator.allocate("large", 100).ok());
}
