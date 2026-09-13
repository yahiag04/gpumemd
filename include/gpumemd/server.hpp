#pragma once

#include "gpumemd/resource_manager.hpp"

#include <atomic>
#include <functional>
#include <string>

namespace gpumemd {

class UnixSocketServer final {
public:
    UnixSocketServer(ResourceManager& manager, std::string socket_path);
    ~UnixSocketServer();

    UnixSocketServer(const UnixSocketServer&) = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;

    // Blocks until request_shutdown() or external_stop returns true.
    int run(const std::function<bool()>& external_stop = {});
    void request_shutdown() noexcept;

private:
    ResourceManager& manager_;
    std::string socket_path_;
    std::atomic<bool> stop_requested_{false};
    int listen_fd_{-1};
};

} // namespace gpumemd
