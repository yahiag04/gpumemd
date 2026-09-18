#include "gpumemd/client.hpp"

#include "gpumemd/protocol.hpp"

#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstring>
#include <sstream>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace gpumemd {
namespace {

int connect_socket(std::string_view path) {
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) return -1;
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.data(), path.size());
    if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool write_all(int fd, std::string_view data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = write(fd, data.data() + offset, data.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool is_multiline(std::string_view command) {
    const auto separator = command.find_first_of(" \t");
    const auto verb = command.substr(0, separator);
    return verb == "status" || verb == "models" || verb == "residency" ||
           verb == "metrics" || verb == "nodes";
}

} // namespace

Client::Client(std::string socket_path) : socket_path_(std::move(socket_path)) {}

std::optional<ShareDescriptor> parse_share_response(std::string_view response) {
    std::istringstream input{std::string(response)};
    std::string ok;
    std::string action;
    std::string name;
    std::string bytes_token;
    std::string token;
    if (!(input >> ok >> action >> name >> bytes_token >> token) || ok != "OK" ||
        action != "shared" || token.empty()) {
        return std::nullopt;
    }
    Bytes bytes = 0;
    const auto parsed = std::from_chars(bytes_token.data(),
                                        bytes_token.data() + bytes_token.size(), bytes);
    if (parsed.ec != std::errc{} || parsed.ptr != bytes_token.data() + bytes_token.size()) {
        return std::nullopt;
    }
    for (const char character : token) {
        if (!std::isxdigit(static_cast<unsigned char>(character))) return std::nullopt;
    }
    return ShareDescriptor{bytes, std::move(token)};
}

ClientResponse Client::request(std::string_view command) const {
    if (command.empty() || command.find('\n') != std::string_view::npos) {
        return {ClientError::InvalidRequest, {}};
    }
    const int fd = connect_socket(socket_path_);
    if (fd < 0) return {ClientError::ConnectionFailed, {}};

    const std::string wire = std::string(command) + '\n';
    if (!write_all(fd, wire)) {
        close(fd);
        return {ClientError::WriteFailed, {}};
    }

    std::string response;
    char buffer[4096];
    const bool multiline = is_multiline(command);
    while (true) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            response.append(buffer, static_cast<std::size_t>(count));
            if ((response.starts_with("ERR ") && response.find('\n') != std::string::npos) ||
                (multiline && response.ends_with("END\n")) ||
                (!multiline && response.find('\n') != std::string::npos)) {
                break;
            }
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            close(fd);
            return {ClientError::ReadFailed, std::move(response)};
        }
    }
    close(fd);
    if (response.starts_with("ERR ")) return {ClientError::BrokerRejected, std::move(response)};
    if (!response.starts_with("OK ")) return {ClientError::ReadFailed, std::move(response)};
    return {ClientError::None, std::move(response)};
}

ClientResponse Client::acquire(std::string_view name, Bytes bytes,
                               AcquireOptions options) const {
    std::ostringstream command;
    command << "acquire " << name << ' ' << bytes << ' ' << options.priority;
    if (options.timeout != std::chrono::milliseconds::max()) {
        command << ' ' << options.timeout.count();
        if (options.estimated_cost.count() != 0) command << ' ' << options.estimated_cost.count();
    }
    return request(command.str());
}

ClientResponse Client::try_acquire(std::string_view name, Bytes bytes) const {
    return request("try_acquire " + std::string(name) + ' ' + std::to_string(bytes));
}

ClientResponse Client::release(std::string_view name) const {
    return request("release " + std::string(name));
}

ClientResponse Client::load(std::string_view name) const {
    return request("load " + std::string(name));
}

ClientResponse Client::unload(std::string_view name) const {
    return request("unload " + std::string(name));
}

ClientResponse Client::share(std::string_view name) const {
    return request("share " + std::string(name));
}

} // namespace gpumemd
