#include "gpumemd/protocol.hpp"

#include <cassert>
#include <limits>
#include <string>

using gpumemd::Bytes;
using gpumemd::CommandType;
using gpumemd::AcquireMode;
using gpumemd::ParseError;
using gpumemd::format_operation_result;
using gpumemd::format_parse_error;
using gpumemd::format_status;
using gpumemd::OperationResult;
using gpumemd::StatusSnapshot;
using gpumemd::Reservation;
using gpumemd::parse_command;
using gpumemd::parse_bytes;

void parses_supported_commands_and_units() {
    auto result = parse_command(" acquire\tprocessA\t4GB ");
    assert(result.ok());
    assert(result.command.type == CommandType::Acquire);
    assert(result.command.name == "processA");
    assert(result.command.bytes == 4'000'000'000ULL);

    result = parse_command("acquire worker 2GiB");
    assert(result.ok());
    assert(result.command.bytes == 2ULL * 1024 * 1024 * 1024);

    result = parse_command("acquire bytes 123B");
    assert(result.ok() && result.command.bytes == 123);
    result = parse_command("acquire bare 123");
    assert(result.ok() && result.command.bytes == 123);
    result = parse_command("release processA");
    assert(result.ok() && result.command.type == CommandType::Release);
    result = parse_command("status");
    assert(result.ok() && result.command.type == CommandType::Status);
}

void rejects_malformed_commands() {
    auto result = parse_command("");
    assert(!result.ok() && result.error == ParseError::InvalidRequest);
    result = parse_command("status extra");
    assert(!result.ok() && result.error == ParseError::InvalidRequest);
    result = parse_command("acquire only_name");
    assert(!result.ok() && result.error == ParseError::InvalidRequest);
    result = parse_command("release");
    assert(!result.ok() && result.error == ParseError::InvalidRequest);
    result = parse_command("unknown process 1");
    assert(!result.ok() && result.error == ParseError::InvalidRequest);
}

void rejects_invalid_names_and_sizes() {
    auto result = parse_command("acquire bad/name 1GB");
    assert(!result.ok() && result.error == ParseError::InvalidName);
    result = parse_command("acquire processA 0B");
    assert(!result.ok() && result.error == ParseError::InvalidSize);
    result = parse_command("acquire processA -1GB");
    assert(!result.ok() && result.error == ParseError::InvalidSize);
    result = parse_command("acquire processA 1.5GB");
    assert(!result.ok() && result.error == ParseError::InvalidSize);
    result = parse_command("acquire processA 12XB");
    assert(!result.ok() && result.error == ParseError::InvalidSize);
    result = parse_command("acquire processA 18446744073709551616B");
    assert(!result.ok() && result.error == ParseError::InvalidSize);
    result = parse_command("acquire processA 18446744073709551615GB");
    assert(!result.ok() && result.error == ParseError::InvalidSize);
}

void accepts_boundary_name_length() {
    const std::string name(64, 'x');
    const auto result = parse_command("acquire " + name + " 1B");
    assert(result.ok());
    assert(result.command.name == name);
}

void formats_wire_responses() {
    assert(parse_bytes("4GB").value() == 4'000'000'000ULL);
    assert(parse_bytes("2GiB").value() == 2ULL * 1024 * 1024 * 1024);

    assert(format_operation_result("acquired", "processA",
                                   OperationResult{gpumemd::ErrorCode::None, 4000}) ==
           "OK acquired processA 4000\n");
    assert(format_operation_result("released", "processA",
                                   OperationResult{gpumemd::ErrorCode::UnknownClient, 0}) ==
           "ERR unknown_client client not found\n");
    assert(format_parse_error(ParseError::InvalidSize) ==
           "ERR invalid_size invalid memory size\n");
    assert(format_operation_result("acquired", "waiter",
                                   OperationResult{gpumemd::ErrorCode::Timeout, 0}) ==
           "ERR timeout acquire request timed out\n");
    assert(format_operation_result("acquired", "waiter",
                                   OperationResult{gpumemd::ErrorCode::InvalidTimeout, 0}) ==
           "ERR invalid_timeout timeout must be nonnegative milliseconds\n");

    const StatusSnapshot snapshot{
        16'000, 12'000, 4'000, {Reservation{"processA", 4'000}, Reservation{"processB", 8'000}}};
    assert(format_status(snapshot) ==
           "OK status 16000 12000 4000 2\n"
           "CLIENT processA 4000\n"
           "CLIENT processB 8000\n"
           "END\n");
}

void parses_wait_options_and_try_acquire() {
    auto result = parse_command("acquire processA 1GB -4 2500");
    assert(result.ok());
    assert(result.command.acquire_mode == AcquireMode::Wait);
    assert(result.command.options.priority == -4);
    assert(result.command.options.timeout == std::chrono::milliseconds(2500));

    result = parse_command("try_acquire processB 2MB");
    assert(result.ok());
    assert(result.command.acquire_mode == AcquireMode::Try);
    assert(result.command.options.timeout.count() == 0);

    result = parse_command("acquire processA 1GB 10");
    assert(result.ok() && result.command.options.priority == 10);
    result = parse_command("acquire processA 1GB 10 0");
    assert(result.ok() && result.command.options.timeout.count() == 0);
    result = parse_command("acquire processA 1B -2147483648 0");
    assert(result.ok() && result.command.options.priority == std::numeric_limits<int>::min());
    result = parse_command("acquire processA 1B +2147483647 0");
    assert(result.ok() && result.command.options.priority == std::numeric_limits<int>::max());
}

void rejects_invalid_wait_options() {
    auto result = parse_command("try_acquire processA 1GB 1");
    assert(!result.ok() && result.error == ParseError::InvalidRequest);
    result = parse_command("acquire processA 1GB nope");
    assert(!result.ok() && result.error == ParseError::InvalidRequest);
    result = parse_command("acquire processA 1GB 1 -5");
    assert(!result.ok() && result.error == ParseError::InvalidRequest);
    result = parse_command("acquire processA 1GB 1 18446744073709551615");
    assert(!result.ok() && result.error == ParseError::InvalidRequest);
}

int main() {
    parses_supported_commands_and_units();
    rejects_malformed_commands();
    rejects_invalid_names_and_sizes();
    accepts_boundary_name_length();
    formats_wire_responses();
    parses_wait_options_and_try_acquire();
    rejects_invalid_wait_options();
}
