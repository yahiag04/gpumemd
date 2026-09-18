#include "gpumemd/server.hpp"

#include "gpumemd/protocol.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

namespace gpumemd {
namespace {

constexpr std::size_t kMaxLineBytes = 4096;

struct Client {
    int fd;
    std::string input;
};

bool process_input(Client& client, ResourceManager& manager, ModelRegistry& registry,
                   ModelResidencyManager& residency, Metrics* metrics, NodeRegistry* nodes);

bool send_all(int fd, const std::string& response) noexcept {
    std::size_t sent = 0;
    while (sent < response.size()) {
        const ssize_t count = write(fd, response.data() + sent, response.size() - sent);
        if (count > 0) {
            sent += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

std::string dispatch(ResourceManager& manager, ModelRegistry& registry,
                     ModelResidencyManager& residency, const Command& command,
                     Metrics* metrics, NodeRegistry* nodes) {
    switch (command.type) {
    case CommandType::Acquire:
        if (command.acquire_mode == AcquireMode::Try) {
            const OperationResult result = manager.try_acquire(command.name, command.bytes);
            if (metrics != nullptr) {
                metrics->record_acquire(result.ok(), result.amount);
            }
            return format_operation_result("acquired", command.name, result);
        }
        {
            const OperationResult result =
                manager.acquire(command.name, command.bytes, command.options);
            if (metrics != nullptr) {
                metrics->record_acquire(result.ok(), result.amount);
            }
            return format_operation_result("acquired", command.name, result);
        }
    case CommandType::Release:
        {
            const OperationResult result = manager.release(command.name);
            if (metrics != nullptr) {
                metrics->record_release(result.ok(), result.amount);
            }
            return format_operation_result("released", command.name, result);
        }
    case CommandType::Status:
        return format_status(manager.status());
    case CommandType::RegisterModel:
        return format_model_operation_result(
            "registered", command.name,
            registry.register_model(command.name, command.bytes, command.metadata));
    case CommandType::RegisterFile:
        return format_model_operation_result(
            "registered", command.name,
            registry.register_file(command.name, command.path, command.metadata));
    case CommandType::UnregisterModel:
        return format_model_operation_result("unregistered", command.name,
                                             residency.unregister_model(command.name));
    case CommandType::RetainModel:
        return format_model_operation_result("retained", command.name,
                                             residency.retain(command.name));
    case CommandType::ReleaseModel:
        return format_model_operation_result("released_model", command.name,
                                             residency.release_model(command.name));
    case CommandType::Models:
        return format_models(registry.models());
    case CommandType::LoadModel:
        {
            const ModelOperationResult result = residency.load(command.name);
            if (metrics != nullptr) {
                metrics->record_model_load(result.ok());
            }
            return format_residency_operation_result("loaded", command.name, result);
        }
    case CommandType::UnloadModel:
        {
            const ModelOperationResult result = residency.unload(command.name);
            if (metrics != nullptr) {
                metrics->record_model_unload(result.ok());
            }
            return format_residency_operation_result("unloaded", command.name, result);
        }
    case CommandType::Residency:
        return format_residency(residency.residency());
    case CommandType::Share:
        return format_share_result(command.name, residency.share(command.name));
    case CommandType::Metrics:
        return metrics == nullptr ? "ERR metrics_unavailable metrics are disabled\n"
                                  : format_metrics(metrics->snapshot());
    case CommandType::RegisterNode:
        return nodes == nullptr
                   ? "ERR nodes_unavailable node registry is disabled\n"
                   : format_node_operation_result(
                         "node_registered", command.name,
                         nodes->register_node(command.name, command.path, command.bytes));
    case CommandType::UnregisterNode:
        return nodes == nullptr
                   ? "ERR nodes_unavailable node registry is disabled\n"
                   : format_node_operation_result("node_removed", command.name,
                                                  nodes->unregister_node(command.name));
    case CommandType::Nodes:
        return nodes == nullptr ? "ERR nodes_unavailable node registry is disabled\n"
                                : format_nodes(nodes->nodes());
    }
    return "ERR internal_error unknown command type\n";
}

void client_session(int fd, ResourceManager& manager, ModelRegistry& registry,
                    ModelResidencyManager& residency,
                    Metrics* metrics, NodeRegistry* nodes,
                    const std::atomic<bool>& stop_requested) {
    timeval timeout{0, 100'000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    Client client{fd, {}};
    char buffer[4096];
    while (!stop_requested.load(std::memory_order_relaxed)) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            client.input.append(buffer, static_cast<std::size_t>(count));
            if (!process_input(client, manager, registry, residency, metrics, nodes)) {
                break;
            }
        } else if (count == 0) {
            if (!client.input.empty()) {
                send_all(fd, "ERR invalid_request unterminated command\n");
            }
            break;
        } else if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        } else {
            break;
        }
    }
    close(fd);
}

bool process_input(Client& client, ResourceManager& manager, ModelRegistry& registry,
                   ModelResidencyManager& residency, Metrics* metrics, NodeRegistry* nodes) {
    while (true) {
        const std::size_t newline = client.input.find('\n');
        if (newline == std::string::npos) {
            if (client.input.size() >= kMaxLineBytes) {
                send_all(client.fd,
                         "ERR request_too_large request exceeds 4096 bytes\n");
                return false;
            }
            return true;
        }

        if (newline + 1 > kMaxLineBytes) {
            send_all(client.fd,
                     "ERR request_too_large request exceeds 4096 bytes\n");
            return false;
        }

        std::string line = client.input.substr(0, newline);
        client.input.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const ParseResult parsed = parse_command(line);
        const std::string response = parsed.ok()
                                          ? dispatch(manager, registry, residency,
                                                     parsed.command, metrics, nodes)
                                          : format_parse_error(parsed.error);
        if (metrics != nullptr) {
            metrics->record_request(response.starts_with("OK"));
        }
        if (!send_all(client.fd, response)) {
            return false;
        }
    }
}

} // namespace

UnixSocketServer::UnixSocketServer(ResourceManager& manager, ModelRegistry& registry,
                                   ModelResidencyManager& residency,
                                   std::string socket_path, Metrics* metrics, NodeRegistry* nodes)
    : manager_(manager), registry_(registry), residency_(residency),
      socket_path_(std::move(socket_path)), metrics_(metrics), nodes_(nodes) {}

UnixSocketServer::~UnixSocketServer() {
    request_shutdown();
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

void UnixSocketServer::request_shutdown() noexcept {
    stop_requested_.store(true, std::memory_order_relaxed);
}

int UnixSocketServer::run(const std::function<bool()>& external_stop) {
    stop_requested_.store(false, std::memory_order_relaxed);

    if (socket_path_.empty() || socket_path_.size() >= sizeof(sockaddr_un::sun_path)) {
        return -1;
    }
    struct stat existing {};
    if (lstat(socket_path_.c_str(), &existing) == 0 || errno != ENOENT) {
        return -1;
    }

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return -1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path_.c_str(), sizeof(address.sun_path) - 1);
    if (bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return -1;
    }
    if (chmod(socket_path_.c_str(), S_IRUSR | S_IWUSR) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        unlink(socket_path_.c_str());
        return -1;
    }
    if (listen(listen_fd_, 16) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        unlink(socket_path_.c_str());
        return -1;
    }

    std::vector<std::thread> workers;
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        if (external_stop && external_stop()) {
            request_shutdown();
            break;
        }

        pollfd listener{listen_fd_, POLLIN, 0};
        const int ready = poll(&listener, 1, 100);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;
        }

        if (listener.revents & POLLIN) {
            const int client_fd = accept(listen_fd_, nullptr, nullptr);
            if (client_fd >= 0) {
                workers.emplace_back(client_session, client_fd, std::ref(manager_),
                                     std::ref(registry_), std::ref(residency_),
                                     metrics_, nodes_,
                                     std::cref(stop_requested_));
            }
        }
    }

    close(listen_fd_);
    listen_fd_ = -1;
    manager_.cancel_waiters();
    for (auto& worker : workers) {
        worker.join();
    }
    unlink(socket_path_.c_str());
    return 0;
}

} // namespace gpumemd
