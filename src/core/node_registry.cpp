#include "gpumemd/node_registry.hpp"

#include <algorithm>
#include <cctype>

namespace gpumemd {

bool NodeRegistry::valid_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 64) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
    });
}

bool NodeRegistry::valid_endpoint(std::string_view endpoint) noexcept {
    if (endpoint.empty() || endpoint.size() > 256) return false;
    return std::all_of(endpoint.begin(), endpoint.end(), [](char c) {
        return static_cast<unsigned char>(c) > 32 && c != '\n';
    });
}

NodeOperationResult NodeRegistry::register_node(std::string_view id,
                                                 std::string_view endpoint,
                                                 Bytes capacity) {
    if (!valid_id(id)) return {NodeError::InvalidId, 0};
    if (!valid_endpoint(endpoint)) return {NodeError::InvalidEndpoint, 0};
    if (capacity == 0) return {NodeError::InvalidCapacity, 0};
    std::lock_guard lock(mutex_);
    if (nodes_.contains(std::string(id))) return {NodeError::DuplicateNode, 0};
    nodes_.emplace(std::string(id), NodeRecord{std::string(id), std::string(endpoint), capacity});
    return {NodeError::None, capacity};
}

NodeOperationResult NodeRegistry::unregister_node(std::string_view id) {
    std::lock_guard lock(mutex_);
    const auto erased = nodes_.erase(std::string(id));
    return erased == 0 ? NodeOperationResult{NodeError::UnknownNode, 0}
                       : NodeOperationResult{NodeError::None, 0};
}

NodeOperationResult NodeRegistry::update_usage(std::string_view id, Bytes used) {
    std::lock_guard lock(mutex_);
    const auto iterator = nodes_.find(std::string(id));
    if (iterator == nodes_.end()) return {NodeError::UnknownNode, 0};
    if (used > iterator->second.capacity) return {NodeError::InvalidCapacity, 0};
    iterator->second.used = used;
    return {NodeError::None, used};
}

NodeOperationResult NodeRegistry::set_health(std::string_view id, bool healthy) {
    std::lock_guard lock(mutex_);
    const auto iterator = nodes_.find(std::string(id));
    if (iterator == nodes_.end()) return {NodeError::UnknownNode, 0};
    iterator->second.healthy = healthy;
    return {NodeError::None, 0};
}

NodeSnapshot NodeRegistry::nodes() const {
    std::lock_guard lock(mutex_);
    NodeSnapshot snapshot;
    for (const auto& [id, node] : nodes_) snapshot.nodes.push_back(node);
    std::sort(snapshot.nodes.begin(), snapshot.nodes.end(),
              [](const NodeRecord& left, const NodeRecord& right) { return left.id < right.id; });
    return snapshot;
}

} // namespace gpumemd
