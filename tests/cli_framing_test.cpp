#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::string socket_path() {
    return "/tmp/gpumemctl-framing-" +
           std::to_string(static_cast<long long>(getpid())) + ".sock";
}

int create_listener(const std::string& path) {
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    assert(path.size() < sizeof(address.sun_path));
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", path.c_str());
    assert(bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    assert(listen(fd, 1) == 0);
    return fd;
}

void write_all(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = write(fd, data.data() + offset, data.size() - offset);
        if (count <= 0) {
            return;
        }
        offset += static_cast<std::size_t>(count);
    }
}

struct ClientResult {
    int exit_code;
    std::string output;
};

ClientResult run_client(const std::string& executable, const std::string& path,
                        const char* command) {
    int output_pipe[2];
    assert(pipe(output_pipe) == 0);
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(output_pipe[0]);
        assert(dup2(output_pipe[1], STDOUT_FILENO) >= 0);
        close(output_pipe[1]);
        execl(executable.c_str(), executable.c_str(), "--socket", path.c_str(),
              command, static_cast<char*>(nullptr));
        _exit(127);
    }
    close(output_pipe[1]);
    std::string output;
    char buffer[1024];
    ssize_t count = 0;
    while ((count = read(output_pipe[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, static_cast<std::size_t>(count));
    }
    close(output_pipe[0]);
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    return {WEXITSTATUS(status), output};
}

} // namespace

void fragmented_models_response_is_read_through_end(const std::string& executable,
                                                    const std::string& path) {
    unlink(path.c_str());
    const int listener = create_listener(path);

    std::string expected = "OK models 180\n";
    for (int index = 0; index < 180; ++index) {
        expected += "MODEL model" + std::to_string(index) +
                    " metadata-for-framing-test 123456789 1 1\n";
    }
    expected += "END\n";
    assert(expected.size() > 4096);

    std::thread server([&] {
        const int client = accept(listener, nullptr, nullptr);
        assert(client >= 0);
        char command[64];
        assert(read(client, command, sizeof(command)) > 0);
        const std::size_t first_newline = expected.find('\n') + 1;
        write_all(client, expected.substr(0, first_newline));
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        for (std::size_t offset = first_newline; offset < expected.size(); offset += 701) {
            write_all(client, expected.substr(offset, 701));
        }
        close(client);
    });

    const ClientResult result = run_client(executable, path, "models");
    server.join();
    close(listener);
    unlink(path.c_str());
    assert(result.exit_code == 0);
    assert(result.output == expected);
}

void eof_before_status_end_is_an_error(const std::string& executable,
                                       const std::string& path) {
    const int listener = create_listener(path);
    std::thread server([&] {
        const int client = accept(listener, nullptr, nullptr);
        assert(client >= 0);
        char command[64];
        assert(read(client, command, sizeof(command)) > 0);
        write_all(client, "OK status 100 0 100 0\n");
        close(client);
    });
    const ClientResult result = run_client(executable, path, "status");
    server.join();
    close(listener);
    unlink(path.c_str());
    assert(result.exit_code != 0);
}

void error_response_returns_before_eof(const std::string& executable,
                                       const std::string& path) {
    const int listener = create_listener(path);
    std::thread server([&] {
        const int client = accept(listener, nullptr, nullptr);
        assert(client >= 0);
        char command[64];
        assert(read(client, command, sizeof(command)) > 0);
        write_all(client, "ERR invalid_request rejected\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        close(client);
    });
    const auto started = std::chrono::steady_clock::now();
    const ClientResult result = run_client(executable, path, "models");
    const auto elapsed = std::chrono::steady_clock::now() - started;
    server.join();
    close(listener);
    unlink(path.c_str());
    assert(result.exit_code != 0);
    assert(result.output == "ERR invalid_request rejected\n");
    assert(elapsed < std::chrono::milliseconds(500));
}

int main(int argc, char* argv[]) {
    assert(argc == 2);
    std::signal(SIGPIPE, SIG_IGN);
    const std::string path = socket_path();
    fragmented_models_response_is_read_through_end(argv[1], path);
    eof_before_status_end_is_an_error(argv[1], path);
    error_response_returns_before_eof(argv[1], path);
}
