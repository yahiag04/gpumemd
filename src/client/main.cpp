#include "gpumemd/protocol.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

void print_usage(std::ostream& output) {
    output << "Usage: gpumemctl --socket PATH COMMAND [ARGUMENTS...]\n\n"
              "Commands: acquire NAME SIZE [PRIORITY] [TIMEOUT_MS],\n"
              "          try_acquire NAME SIZE, release NAME, status\n";
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

int connect_socket(std::string_view path) {
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
        return -1;
    }
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.data(), sizeof(address.sun_path) - 1);
    if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        print_usage(std::cout);
        return 0;
    }
    if (argc < 4 || std::string_view{argv[1]} != "--socket") {
        print_usage(std::cerr);
        return 2;
    }

    std::string command;
    for (int index = 3; index < argc; ++index) {
        if (!command.empty()) {
            command += ' ';
        }
        command += argv[index];
    }
    const auto parsed = gpumemd::parse_command(command);
    if (!parsed.ok()) {
        std::cerr << gpumemd::format_parse_error(parsed.error);
        return 2;
    }

    const int fd = connect_socket(argv[2]);
    if (fd < 0) {
        std::cerr << "gpumemctl: could not connect to socket '" << argv[2] << "'\n";
        return 1;
    }

    const std::string wire = command + '\n';
    if (!write_all(fd, wire)) {
        close(fd);
        std::cerr << "gpumemctl: could not send command\n";
        return 1;
    }

    std::string response;
    char buffer[4096];
    while (true) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            response.append(buffer, static_cast<std::size_t>(count));
            if ((parsed.command.type == gpumemd::CommandType::Status && response.ends_with("END\n")) ||
                (parsed.command.type != gpumemd::CommandType::Status &&
                 response.find('\n') != std::string::npos)) {
                break;
            }
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    close(fd);
    std::cout << response;
    return response.starts_with("OK ") ? 0 : 1;
}
