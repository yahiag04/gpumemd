#pragma once

#include "gpumemd/client.hpp"
#include "gpumemd/cuda_ipc_client.hpp"

#include <memory>
#include <string>

namespace gpumemd {

class CudaIpcAllocation final {
public:
    CudaIpcAllocation(void* pointer, Bytes bytes);
    ~CudaIpcAllocation();

    CudaIpcAllocation(const CudaIpcAllocation&) = delete;
    CudaIpcAllocation& operator=(const CudaIpcAllocation&) = delete;
    CudaIpcAllocation(CudaIpcAllocation&& other) noexcept;
    CudaIpcAllocation& operator=(CudaIpcAllocation&& other) noexcept;

    [[nodiscard]] void* pointer() const noexcept { return pointer_; }
    [[nodiscard]] Bytes bytes() const noexcept { return bytes_; }

private:
    void reset() noexcept;

    CudaIpcClient ipc_;
    void* pointer_{nullptr};
    Bytes bytes_{0};
};

struct CudaOpenResult {
    ClientResponse response;
    BackendOperationResult backend;
    std::unique_ptr<CudaIpcAllocation> allocation;

    [[nodiscard]] bool ok() const noexcept {
        return response.ok() && backend.ok() && allocation != nullptr;
    }
};

class CudaClient final {
public:
    explicit CudaClient(std::string socket_path);

    [[nodiscard]] CudaOpenResult open_model(std::string_view name) const;

private:
    Client broker_;
    CudaIpcClient ipc_;
};

} // namespace gpumemd
