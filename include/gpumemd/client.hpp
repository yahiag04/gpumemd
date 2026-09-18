#pragma once

#include "gpumemd/memory.hpp"
#include "gpumemd/resource_manager.hpp"

#include <string>
#include <string_view>
#include <optional>

namespace gpumemd {

enum class ClientError { None, InvalidRequest, ConnectionFailed, WriteFailed, ReadFailed,
                         BrokerRejected };

struct ClientResponse {
    ClientError error{ClientError::None};
    std::string payload;

    [[nodiscard]] bool ok() const noexcept { return error == ClientError::None; }
};

struct ShareDescriptor {
    Bytes bytes{0};
    std::string token;
};

[[nodiscard]] std::optional<ShareDescriptor> parse_share_response(
    std::string_view response);

class Client final {
public:
    explicit Client(std::string socket_path);

    [[nodiscard]] ClientResponse request(std::string_view command) const;
    [[nodiscard]] ClientResponse acquire(std::string_view name, Bytes bytes,
                                          AcquireOptions options = {}) const;
    [[nodiscard]] ClientResponse try_acquire(std::string_view name, Bytes bytes) const;
    [[nodiscard]] ClientResponse release(std::string_view name) const;
    [[nodiscard]] ClientResponse load(std::string_view name) const;
    [[nodiscard]] ClientResponse unload(std::string_view name) const;
    [[nodiscard]] ClientResponse share(std::string_view name) const;

private:
    std::string socket_path_;
};

} // namespace gpumemd
