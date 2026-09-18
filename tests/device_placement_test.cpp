#include "gpumemd/device_placement.hpp"

#include <cassert>

int main() {
    gpumemd::DevicePlacement placement({100, 200});
    assert(placement.place("large", 150).device == 1);
    assert(placement.place("small", 80).device == 0);
    assert(placement.place("another", 70).device == 0);
    assert(placement.place("full", 100).error == gpumemd::PlacementError::InsufficientMemory);
    assert(placement.release("large").device == 1);
    const auto snapshot = placement.snapshot();
    assert(snapshot.devices[1].used == 0);
    assert(snapshot.devices[1].free == 200);
}
