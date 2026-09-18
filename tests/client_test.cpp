#include "gpumemd/client.hpp"
#include "gpumemd/model_registry.hpp"
#include "gpumemd/model_residency.hpp"
#include "gpumemd/resource_manager.hpp"
#include "gpumemd/server.hpp"

#include <cassert>
#include <chrono>
#include <string>
#include <thread>

#include <unistd.h>

namespace {

std::string socket_path() {
    return "/tmp/gpumemd-client-" + std::to_string(static_cast<long long>(getpid())) + ".sock";
}

} // namespace

int main() {
    const auto descriptor = gpumemd::parse_share_response(
        "OK shared bert 4096 0011aabb\n");
    assert(descriptor.has_value());
    assert(descriptor->bytes == 4096);
    assert(descriptor->token == "0011aabb");
    assert(!gpumemd::parse_share_response("ERR unknown_residency model is not resident\n"));

    const std::string path = socket_path();
    unlink(path.c_str());
    gpumemd::ResourceManager manager(1024);
    gpumemd::ModelRegistry registry;
    gpumemd::ModelResidencyManager residency(registry, manager);
    gpumemd::UnixSocketServer server(manager, registry, residency, path);
    std::thread server_thread([&] { assert(server.run() == 0); });

    gpumemd::Client client(path);
    for (int attempt = 0; attempt < 100 && access(path.c_str(), F_OK) != 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(client.acquire("worker", 100).ok());
    const auto status = client.request("status");
    assert(status.ok());
    assert(status.payload.find("CLIENT worker 100\n") != std::string::npos);
    assert(client.release("worker").ok());
    assert(!client.request("bad-command").ok());

    server.request_shutdown();
    server_thread.join();
    assert(access(path.c_str(), F_OK) != 0);
}
