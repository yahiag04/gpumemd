#pragma once

#include "gpumemd/memory.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gpumemd {

enum class NodeError { None, InvalidId, InvalidEndpoint, InvalidCapacity, DuplicateNode,
                       UnknownNode };

struct NodeOperationResult {
    NodeError error{NodeError::None};
    Bytes amount{0};
    [[nodiscard]] bool ok() const noexcept { return error == NodeError::None; }
};

struct NodeRecord {
    std::string id;
    std::string endpoint;
    Bytes capacity{0};
    Bytes used{0};
    bool healthy{true};
};

struct NodeSnapshot {
    std::vector<NodeRecord> nodes;
};

class NodeRegistry {
public:
    [[nodiscard]] NodeOperationResult register_node(std::string_view id,
                                                     std::string_view endpoint,
                                                     Bytes capacity);
    [[nodiscard]] NodeOperationResult unregister_node(std::string_view id);
    [[nodiscard]] NodeOperationResult update_usage(std::string_view id, Bytes used);
    [[nodiscard]] NodeOperationResult set_health(std::string_view id, bool healthy);
    [[nodiscard]] NodeSnapshot nodes() const;

private:
    static bool valid_id(std::string_view id) noexcept;
    static bool valid_endpoint(std::string_view endpoint) noexcept;

    std::unordered_map<std::string, NodeRecord> nodes_;
    mutable std::mutex mutex_;
};

} // namespace gpumemd
