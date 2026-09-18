#include "gpumemd/cuda_backend.hpp"
#include "gpumemd/cuda_ipc_client.hpp"

#include <cassert>
#include <cstdlib>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "--consumer") {
        gpumemd::CudaIpcClient client;
        void* pointer = nullptr;
        const auto opened = client.open(argv[2], pointer);
        if (!opened.ok()) {
            return 1;
        }
        return client.close(pointer).ok() ? 0 : 1;
    }

    if (!gpumemd::CUDABackend::is_available()) {
        return 0;
    }

    gpumemd::CUDABackend backend;
    assert(backend.load("shared", 4096).ok());
    const auto shared = backend.share("shared");
    assert(shared.ok());

    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        execl(argv[0], argv[0], "--consumer", shared.token.c_str(), nullptr);
        _exit(127);
    }

    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    assert(backend.unload("shared").ok());
}
