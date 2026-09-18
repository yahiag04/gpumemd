#include "gpumemd/node_registry.hpp"

#include <cassert>

using namespace gpumemd;

int main() {
    NodeRegistry registry;
    assert(registry.register_node("node-a", "tcp://127.0.0.1:9000", 100).ok());
    assert(registry.register_node("node-a", "tcp://127.0.0.1:9001", 100).error ==
           NodeError::DuplicateNode);
    assert(registry.update_usage("node-a", 40).ok());
    assert(registry.set_health("node-a", false).ok());
    const auto snapshot = registry.nodes();
    assert(snapshot.nodes.size() == 1);
    assert(snapshot.nodes[0].used == 40);
    assert(!snapshot.nodes[0].healthy);
    assert(registry.unregister_node("node-a").ok());
    assert(registry.nodes().nodes.empty());
}
