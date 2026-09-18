#include "gpumemd/cuda_backend.hpp"
#include "gpumemd/mock_backend.hpp"
#include "gpumemd/model_registry.hpp"
#include "gpumemd/model_residency.hpp"

#if defined(GPUMEMD_HAS_METAL)
#include "gpumemd/metal_backend.hpp"
#endif

#include <chrono>
#include <charconv>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

void usage(std::ostream& output) {
    output << "Usage: gpumemd-bench --file PATH [--iterations N] "
              "[--backend auto|mock|metal|cuda]\n";
}

bool parse_iterations(std::string_view token, std::size_t& result) {
    std::size_t value = 0;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || value == 0) {
        return false;
    }
    result = value;
    return true;
}

double average_ms(const std::vector<double>& samples) {
    double total = 0.0;
    for (const double sample : samples) {
        total += sample;
    }
    return total / static_cast<double>(samples.size());
}

} // namespace

int main(int argc, char* argv[]) {
    std::string file_path;
    std::string backend_name = "auto";
    std::size_t iterations = 3;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if ((argument == "--file" || argument == "--iterations" || argument == "--backend") &&
            index + 1 < argc) {
            const std::string value{argv[++index]};
            if (argument == "--file") {
                file_path = value;
            } else if (argument == "--backend") {
                backend_name = value;
            } else if (!parse_iterations(value, iterations)) {
                std::cerr << "gpumemd-bench: invalid iteration count\n";
                return 2;
            }
        } else if (argument == "--help") {
            usage(std::cout);
            return 0;
        } else {
            usage(std::cerr);
            return 2;
        }
    }
    if (file_path.empty() || (backend_name != "auto" && backend_name != "mock" &&
                              backend_name != "metal" && backend_name != "cuda")) {
        usage(std::cerr);
        return 2;
    }

    gpumemd::MockBackend mock_backend;
    gpumemd::AcceleratorBackend* backend = &mock_backend;
    std::string selected_backend = "mock";
#if defined(GPUMEMD_HAS_CUDA)
    std::unique_ptr<gpumemd::CUDABackend> cuda_backend;
    if (backend_name == "cuda" || (backend_name == "auto" &&
                                    gpumemd::CUDABackend::is_available())) {
        if (!gpumemd::CUDABackend::is_available()) {
            std::cerr << "gpumemd-bench: CUDA backend is unavailable\n";
            return 1;
        }
        cuda_backend = std::make_unique<gpumemd::CUDABackend>();
        backend = cuda_backend.get();
        selected_backend = "cuda";
    }
#else
    if (backend_name == "cuda") {
        std::cerr << "gpumemd-bench: CUDA backend was not built\n";
        return 1;
    }
#endif
#if defined(GPUMEMD_HAS_METAL)
    std::unique_ptr<gpumemd::MetalBackend> metal_backend;
    if (backend_name == "metal" ||
        (backend_name == "auto" && backend == &mock_backend &&
         gpumemd::MetalBackend::is_available())) {
        if (!gpumemd::MetalBackend::is_available()) {
            std::cerr << "gpumemd-bench: Metal backend is unavailable\n";
            return 1;
        }
        metal_backend = std::make_unique<gpumemd::MetalBackend>();
        backend = metal_backend.get();
        selected_backend = "metal";
    }
#else
    if (backend_name == "metal") {
        std::cerr << "gpumemd-bench: Metal backend was not built\n";
        return 1;
    }
#endif
    if (backend_name == "mock") {
        backend = &mock_backend;
        selected_backend = "mock";
    }

    gpumemd::ModelRegistry registry;
    const auto registered = registry.register_file("benchmark", file_path, "benchmark");
    if (!registered.ok()) {
        std::cerr << "gpumemd-bench: could not register model file\n";
        return 1;
    }
    gpumemd::ResourceManager resources(registered.amount);
    gpumemd::ModelResidencyManager residency(registry, resources, *backend);

    std::vector<double> cold_samples;
    cold_samples.reserve(iterations);
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto start = Clock::now();
        if (!residency.load("benchmark").ok()) {
            std::cerr << "gpumemd-bench: cold load failed\n";
            return 1;
        }
        const auto end = Clock::now();
        cold_samples.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
        if (!residency.unload("benchmark").ok()) {
            std::cerr << "gpumemd-bench: cold unload failed\n";
            return 1;
        }
    }

    if (!residency.load("benchmark").ok()) {
        std::cerr << "gpumemd-bench: warm setup failed\n";
        return 1;
    }
    std::vector<double> warm_samples;
    warm_samples.reserve(iterations);
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto start = Clock::now();
        if (!residency.load("benchmark").ok()) {
            std::cerr << "gpumemd-bench: warm load failed\n";
            return 1;
        }
        const auto end = Clock::now();
        warm_samples.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }
    (void)residency.unload("benchmark");

    std::cout << std::fixed << std::setprecision(3)
              << "backend " << selected_backend << '\n'
              << "bytes " << registered.amount << '\n'
              << "iterations " << iterations << '\n'
              << "cold_avg_ms " << average_ms(cold_samples) << '\n'
              << "warm_avg_ms " << average_ms(warm_samples) << '\n';
    return 0;
}
