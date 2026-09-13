#include "gpumemd/server.hpp"

#include "gpumemd/protocol.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace gpumemd {
namespace {

constexpr std::size_t kMaxLineBytes = 4096;

struct Client {
    int fd;
    std::string input;
};

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

std::string dispatch(ResourceManager& manager, const Command& command) {
    switch (command.type) {
    case CommandType::Acquire:
        return format_operation_result("acquired", command.name,
                                       manager.acquire(command.name, command.bytes));
    case CommandType::Release:
        return format_operation_result("released", command.name,
                                       manager.release(command.name));
    case CommandType::Status:
        return format_status(manager.status());
    }
    return "ERR internal_error unknown command type\n";
}

bool process_input(Client& client, ResourceManager& manager) {
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
                                          ? dispatch(manager, parsed.command)
                                          : format_parse_error(parsed.error);
        if (!send_all(client.fd, response)) {
            return false;
        }
    }
}

} // namespace

UnixSocketServer::UnixSocketServer(ResourceManager& manager, std::string socket_path)
    : manager_(manager), socket_path_(std::move(socket_path)) {}

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

    std::vector<Client> clients;
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        if (external_stop && external_stop()) {
            request_shutdown();
            break;
        }

        std::vector<pollfd> poll_fds;
        poll_fds.reserve(clients.size() + 1);
        poll_fds.push_back({listen_fd_, POLLIN, 0});
        for (const Client& client : clients) {
            poll_fds.push_back({client.fd, POLLIN, 0});
        }

        const int ready = poll(poll_fds.data(), static_cast<nfds_t>(poll_fds.size()), 100);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;
        }

        if (poll_fds[0].revents & POLLIN) {
            const int client_fd = accept(listen_fd_, nullptr, nullptr);
            if (client_fd >= 0) {
                clients.push_back({client_fd, {}});
            }
        }

        const std::size_t polled_client_count = poll_fds.size() - 1;
        for (std::size_t index = polled_client_count; index-- > 0;) {
            const short events = poll_fds[index + 1].revents;
            if (events == 0) {
                continue;
            }

            bool keep = (events & POLLIN) != 0;
            if (keep) {
                char buffer[4096];
                const ssize_t count = read(clients[index].fd, buffer, sizeof(buffer));
                if (count > 0) {
                    clients[index].input.append(buffer, static_cast<std::size_t>(count));
                    keep = process_input(clients[index], manager_);
                } else if (count == 0) {
                    if (!clients[index].input.empty()) {
                        send_all(clients[index].fd,
                                 "ERR invalid_request unterminated command\n");
                    }
                    keep = false;
                } else if (errno != EINTR) {
                    keep = false;
                }
            } else {
                keep = false;
            }

            if (!keep || (events & (POLLHUP | POLLERR | POLLNVAL))) {
                close(clients[index].fd);
                clients.erase(clients.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
    }

    for (const Client& client : clients) {
        close(client.fd);
    }
    clients.clear();
    close(listen_fd_);
    listen_fd_ = -1;
    unlink(socket_path_.c_str());
    return 0;
}

} // namespace gpumemd
