#pragma once

#include "gpumemd/memory.hpp"
#include "gpumemd/model_registry.hpp"
#include "gpumemd/resource_manager.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace gpumemd {

enum class CommandType {
    Acquire,
    Release,
    Status,
    RegisterModel,
    UnregisterModel,
    RetainModel,
    ReleaseModel,
    Models,
};

enum class AcquireMode {
    Wait,
    Try,
};

enum class ParseError {
    None,
    InvalidRequest,
    InvalidSize,
    InvalidName,
    InvalidModelId,
    InvalidMetadata,
};

struct Command {
    CommandType type{CommandType::Status};
    std::string name;
    Bytes bytes{0};
    AcquireMode acquire_mode{AcquireMode::Wait};
    AcquireOptions options{};
    std::string metadata;
};

struct ParseResult {
    ParseError error{ParseError::None};
    Command command;

    [[nodiscard]] bool ok() const noexcept { return error == ParseError::None; }
};

[[nodiscard]] std::optional<Bytes> parse_bytes(std::string_view token);
[[nodiscard]] ParseResult parse_command(std::string_view line);
[[nodiscard]] std::string format_parse_error(ParseError error);
[[nodiscard]] std::string format_operation_result(std::string_view action,
                                                  std::string_view name,
                                                  const OperationResult& result);
[[nodiscard]] std::string format_status(const StatusSnapshot& snapshot);
[[nodiscard]] std::string format_model_operation_result(std::string_view action,
                                                         std::string_view name,
                                                         const ModelOperationResult& result);
[[nodiscard]] std::string format_models(const ModelSnapshot& snapshot);

} // namespace gpumemd
