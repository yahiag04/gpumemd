#include "gpumemd/model_registry.hpp"
#include "gpumemd/model_residency.hpp"
#include "gpumemd/mock_backend.hpp"
#if defined(GPUMEMD_HAS_CUDA)
#include "gpumemd/cuda_backend.hpp"
#endif
#if defined(GPUMEMD_HAS_METAL)
#include "gpumemd/metal_backend.hpp"
#endif
#include "gpumemd/protocol.hpp"
#include "gpumemd/resource_manager.hpp"
#include "gpumemd/server.hpp"

#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void handle_signal(int) {
    stop_requested = 1;
}

void print_usage(std::ostream& output) {
    output << "Usage: gpumemd --memory SIZE --socket PATH\n\n"
              "Start a simulated GPU memory broker.\n"
              "SIZE accepts B, KB, MB, GB, KiB, MiB, and GiB units.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        print_usage(std::cout);
        return 0;
    }

    std::string memory_token;
    std::string socket_path;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if ((argument == "--memory" || argument == "--socket") && index + 1 < argc) {
            const std::string value{argv[++index]};
            if (argument == "--memory") {
                memory_token = value;
            } else {
                socket_path = value;
            }
        } else {
            std::cerr << "gpumemd: expected --memory SIZE and --socket PATH\n";
            print_usage(std::cerr);
            return 2;
        }
    }

    const auto capacity = gpumemd::parse_bytes(memory_token);
    if (!capacity || socket_path.empty()) {
        std::cerr << "gpumemd: invalid or missing memory/socket option\n";
        print_usage(std::cerr);
        return 2;
    }

    try {
        gpumemd::ResourceManager manager(*capacity);
        gpumemd::ModelRegistry registry;
        gpumemd::MockBackend mock_backend;
        gpumemd::AcceleratorBackend* backend = &mock_backend;
#if defined(GPUMEMD_HAS_CUDA)
        std::unique_ptr<gpumemd::CUDABackend> cuda_backend;
        if (gpumemd::CUDABackend::is_available()) {
            cuda_backend = std::make_unique<gpumemd::CUDABackend>();
            backend = cuda_backend.get();
        }
#endif
#if defined(GPUMEMD_HAS_METAL)
        std::unique_ptr<gpumemd::MetalBackend> metal_backend;
        if (backend == &mock_backend && gpumemd::MetalBackend::is_available()) {
            metal_backend = std::make_unique<gpumemd::MetalBackend>();
            backend = metal_backend.get();
        }
#endif
        gpumemd::ModelResidencyManager residency(registry, manager, *backend);
        gpumemd::UnixSocketServer server(manager, registry, residency, socket_path);
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        std::signal(SIGPIPE, SIG_IGN);
        const int result = server.run([] { return stop_requested != 0; });
        if (result != 0) {
            std::cerr << "gpumemd: could not bind socket '" << socket_path << "'\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "gpumemd: " << exception.what() << '\n';
        return 1;
    }
}
