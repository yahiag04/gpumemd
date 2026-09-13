#include "gpumemd/server.hpp"

#include "gpumemd/resource_manager.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <poll.h>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::string socket_path() {
    return "/tmp/gpumemd-test-" + std::to_string(static_cast<long long>(getpid())) + ".sock";
}

int connect_to(const std::string& path) {
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    assert(path.size() < sizeof(address.sun_path));
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", path.c_str());
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
            return fd;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    close(fd);
    assert(false && "server did not become ready");
    return -1;
}

std::string request(int fd, const std::string& command, bool multiline = false) {
    const std::string wire = command + "\n";
    assert(write(fd, wire.data(), wire.size()) == static_cast<ssize_t>(wire.size()));
    std::string response;
    char buffer[256];
    while (true) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        assert(count > 0);
        response.append(buffer, static_cast<std::size_t>(count));
        if ((!multiline && response.find('\n') != std::string::npos) ||
            (multiline && response.ends_with("END\n"))) {
            return response;
        }
    }
}

std::string request_with_timeout(int fd, const std::string& command, int timeout_ms) {
    const std::string wire = command + "\n";
    assert(write(fd, wire.data(), wire.size()) == static_cast<ssize_t>(wire.size()));
    pollfd ready{fd, POLLIN, 0};
    if (poll(&ready, 1, timeout_ms) <= 0) {
        return {};
    }
    char buffer[256];
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count <= 0) {
        return {};
    }
    return {buffer, static_cast<std::size_t>(count)};
}

} // namespace

void serves_multiple_clients_and_preserves_state() {
    const std::string path = socket_path();
    unlink(path.c_str());
    gpumemd::ResourceManager manager(10'000);
    gpumemd::UnixSocketServer server(manager, path);
    int run_result = -1;
    std::thread server_thread([&] { run_result = server.run(); });

    const int first = connect_to(path);
    const int second = connect_to(path);
    struct stat socket_info {};
    assert(stat(path.c_str(), &socket_info) == 0);
    assert((socket_info.st_mode & 0777) == 0600);
    gpumemd::UnixSocketServer conflicting_server(manager, path);
    assert(conflicting_server.run() == -1);
    assert(request(first, "acquire processA 4KB") ==
           "OK acquired processA 4000\n");
    assert(request(second, "acquire processB 2KB") ==
           "OK acquired processB 2000\n");
    assert(request(first, "status", true) ==
           "OK status 10000 6000 4000 2\n"
           "CLIENT processA 4000\n"
           "CLIENT processB 2000\n"
           "END\n");

    close(first);
    close(second);
    server.request_shutdown();
    server_thread.join();
    assert(run_result == 0);
    assert(access(path.c_str(), F_OK) != 0);
}

void reports_parse_and_accounting_errors() {
    const std::string path = socket_path();
    unlink(path.c_str());
    gpumemd::ResourceManager manager(100);
    gpumemd::UnixSocketServer server(manager, path);
    std::thread server_thread([&] { server.run(); });

    const int client = connect_to(path);
    assert(request(client, "acquire owner 75B") == "OK acquired owner 75\n");
    assert(request(client, "acquire owner 1B") ==
           "ERR duplicate_client client already has a reservation\n");
    assert(request(client, "try_acquire other 26B") ==
           "ERR insufficient_memory requested bytes exceed available capacity\n");
    assert(request(client, "bad-command") ==
           "ERR invalid_request invalid command syntax\n");
    close(client);

    server.request_shutdown();
    server_thread.join();
    assert(access(path.c_str(), F_OK) != 0);
}

void blocked_acquire_does_not_stop_other_clients() {
    const std::string path = socket_path();
    unlink(path.c_str());
    gpumemd::ResourceManager manager(100);
    gpumemd::UnixSocketServer server(manager, path);
    std::thread server_thread([&] { server.run(); });

    const int blocker = connect_to(path);
    assert(request(blocker, "acquire blocker 100B") == "OK acquired blocker 100\n");
    const int waiting = connect_to(path);
    std::string waiting_response;
    std::thread waiting_thread([&] {
        waiting_response = request(waiting, "acquire waiter 50B 0 2000");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const int releaser = connect_to(path);
    assert(request_with_timeout(releaser, "release blocker", 500) ==
           "OK released blocker 100\n");
    close(releaser);
    waiting_thread.join();
    assert(waiting_response == "OK acquired waiter 50\n");
    close(waiting);
    close(blocker);

    server.request_shutdown();
    server_thread.join();
    assert(access(path.c_str(), F_OK) != 0);
}

int main() {
    serves_multiple_clients_and_preserves_state();
    reports_parse_and_accounting_errors();
    blocked_acquire_does_not_stop_other_clients();
}
