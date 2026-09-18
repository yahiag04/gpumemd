#pragma once

#include "gpumemd/model_registry.hpp"
#include "gpumemd/model_residency.hpp"
#include "gpumemd/metrics.hpp"
#include "gpumemd/node_registry.hpp"
#include "gpumemd/resource_manager.hpp"

#include <atomic>
#include <functional>
#include <string>

namespace gpumemd {

class UnixSocketServer final {
public:
    UnixSocketServer(ResourceManager& manager, ModelRegistry& registry,
                     ModelResidencyManager& residency, std::string socket_path,
                     Metrics* metrics = nullptr, NodeRegistry* nodes = nullptr);
    ~UnixSocketServer();

    UnixSocketServer(const UnixSocketServer&) = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;

    // Blocks until request_shutdown() or external_stop returns true.
    int run(const std::function<bool()>& external_stop = {});
    void request_shutdown() noexcept;

private:
    ResourceManager& manager_;
    ModelRegistry& registry_;
    ModelResidencyManager& residency_;
    std::string socket_path_;
    Metrics* metrics_{nullptr};
    NodeRegistry* nodes_{nullptr};
    std::atomic<bool> stop_requested_{false};
    int listen_fd_{-1};
};

} // namespace gpumemd
