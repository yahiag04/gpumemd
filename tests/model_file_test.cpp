#include "gpumemd/mock_backend.hpp"
#include "gpumemd/model_registry.hpp"
#include "gpumemd/model_residency.hpp"
#include "gpumemd/resource_manager.hpp"

#include <cassert>
#include <fstream>
#include <string>

#include <unistd.h>

int main() {
    const std::string path = "/tmp/gpumemd-model-" +
                             std::to_string(static_cast<long long>(getpid())) + ".bin";
    {
        std::ofstream output(path, std::ios::binary);
        output << "model";
    }

    gpumemd::ModelRegistry registry;
    const auto registered = registry.register_file("file_model", path, "raw");
    assert(registered.ok());
    assert(registered.amount == 5);
    assert(registry.models().models[0].source_path == path);

    gpumemd::ResourceManager resources(100);
    gpumemd::MockBackend backend;
    gpumemd::ModelResidencyManager residency(registry, resources, backend);
    assert(residency.load("file_model").ok());
    assert(backend.is_loaded("file_model"));
    assert(residency.unload("file_model").ok());
    assert(!backend.is_loaded("file_model"));

    unlink(path.c_str());
}
