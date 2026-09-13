#include "gpumemd/protocol.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace gpumemd {
namespace {

bool valid_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char character) {
        const bool letter = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        return letter || digit || character == '_' || character == '-' || character == '.';
    });
}

std::optional<Bytes> parse_size(std::string_view token) noexcept {
    Bytes multiplier = 1;
    std::string_view digits = token;

    constexpr std::pair<std::string_view, Bytes> suffixes[] = {
        {"KiB", 1024ULL},
        {"MiB", 1024ULL * 1024},
        {"GiB", 1024ULL * 1024 * 1024},
        {"KB", 1000ULL},
        {"MB", 1000ULL * 1000},
        {"GB", 1000ULL * 1000 * 1000},
        {"B", 1ULL},
    };

    for (const auto& [suffix, value] : suffixes) {
        if (token.size() >= suffix.size() && token.ends_with(suffix)) {
            digits = token.substr(0, token.size() - suffix.size());
            multiplier = value;
            break;
        }
    }

    if (digits.empty()) {
        return std::nullopt;
    }
    for (const char character : digits) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
    }

    std::uint64_t value = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || value == 0) {
        return std::nullopt;
    }
    if (value > std::numeric_limits<Bytes>::max() / multiplier) {
        return std::nullopt;
    }

    return static_cast<Bytes>(value * multiplier);
}

std::optional<std::uint64_t> parse_unsigned(std::string_view token) noexcept {
    if (token.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> parse_priority(std::string_view token) noexcept {
    if (token.empty()) {
        return std::nullopt;
    }
    bool negative = false;
    if (token.front() == '-' || token.front() == '+') {
        negative = token.front() == '-';
        token.remove_prefix(1);
    }
    const auto magnitude = parse_unsigned(token);
    if (!magnitude) {
        return std::nullopt;
    }
    constexpr std::uint64_t positive_limit = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    constexpr std::uint64_t negative_limit = positive_limit + 1;
    if (*magnitude > (negative ? negative_limit : positive_limit)) {
        return std::nullopt;
    }
    if (negative) {
        if (*magnitude == negative_limit) {
            return std::numeric_limits<int>::min();
        }
        return -static_cast<int>(*magnitude);
    }
    return static_cast<int>(*magnitude);
}

std::optional<std::chrono::milliseconds> parse_timeout(std::string_view token) noexcept {
    const auto value = parse_unsigned(token);
    if (!value || *value > static_cast<std::uint64_t>(std::chrono::milliseconds::max().count())) {
        return std::nullopt;
    }
    return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(*value));
}

std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    for (std::size_t position = 0; position < line.size();) {
        while (position < line.size() &&
               (line[position] == ' ' || line[position] == '\t')) {
            ++position;
        }
        if (position == line.size()) {
            break;
        }
        const std::size_t start = position;
        while (position < line.size() && line[position] != ' ' && line[position] != '\t') {
            ++position;
        }
        tokens.push_back(line.substr(start, position - start));
    }
    return tokens;
}

ParseResult error(ParseError code) {
    return {code, {}};
}

} // namespace

std::optional<Bytes> parse_bytes(std::string_view token) {
    return parse_size(token);
}

ParseResult parse_command(std::string_view line) {
    const auto tokens = tokenize(line);
    if (tokens.empty()) {
        return error(ParseError::InvalidRequest);
    }

    if (tokens[0] == "status") {
        if (tokens.size() != 1) {
            return error(ParseError::InvalidRequest);
        }
        return {ParseError::None, {CommandType::Status, {}, 0}};
    }

    if (tokens[0] == "release") {
        if (tokens.size() != 2) {
            return error(ParseError::InvalidRequest);
        }
        if (!valid_name(tokens[1])) {
            return error(ParseError::InvalidName);
        }
        return {ParseError::None,
                {CommandType::Release, std::string(tokens[1]), 0}};
    }

    if (tokens[0] == "try_acquire") {
        if (tokens.size() != 3) {
            return error(ParseError::InvalidRequest);
        }
        if (!valid_name(tokens[1])) {
            return error(ParseError::InvalidName);
        }
        const auto bytes = parse_size(tokens[2]);
        if (!bytes) {
            return error(ParseError::InvalidSize);
        }
        return {ParseError::None,
                {CommandType::Acquire, std::string(tokens[1]), *bytes,
                 AcquireMode::Try, AcquireOptions{0, std::chrono::milliseconds(0)}}};
    }

    if (tokens[0] == "acquire") {
        if (tokens.size() < 3 || tokens.size() > 5) {
            return error(ParseError::InvalidRequest);
        }
        if (!valid_name(tokens[1])) {
            return error(ParseError::InvalidName);
        }
        const auto bytes = parse_size(tokens[2]);
        if (!bytes) {
            return error(ParseError::InvalidSize);
        }
        AcquireOptions options;
        if (tokens.size() >= 4) {
            const auto priority = parse_priority(tokens[3]);
            if (!priority) {
                return error(ParseError::InvalidRequest);
            }
            options.priority = *priority;
        }
        if (tokens.size() == 5) {
            const auto timeout = parse_timeout(tokens[4]);
            if (!timeout) {
                return error(ParseError::InvalidRequest);
            }
            options.timeout = *timeout;
        }
        return {ParseError::None,
                {CommandType::Acquire, std::string(tokens[1]), *bytes,
                 AcquireMode::Wait, options}};
    }

    return error(ParseError::InvalidRequest);
}

std::string format_parse_error(ParseError error_code) {
    switch (error_code) {
    case ParseError::InvalidRequest:
        return "ERR invalid_request invalid command syntax\n";
    case ParseError::InvalidSize:
        return "ERR invalid_size invalid memory size\n";
    case ParseError::InvalidName:
        return "ERR invalid_name invalid client name\n";
    case ParseError::None:
        return "";
    }
    return "ERR invalid_request invalid command syntax\n";
}

std::string format_operation_result(std::string_view action,
                                    std::string_view name,
                                    const OperationResult& result) {
    if (result.ok()) {
        std::ostringstream output;
        output << "OK " << action << ' ' << name << ' ' << result.amount << '\n';
        return output.str();
    }

    switch (result.error) {
    case ErrorCode::InvalidName:
        return "ERR invalid_name invalid client name\n";
    case ErrorCode::InvalidSize:
        return "ERR invalid_size invalid memory size\n";
    case ErrorCode::DuplicateClient:
        return "ERR duplicate_client client already has a reservation\n";
    case ErrorCode::UnknownClient:
        return "ERR unknown_client client not found\n";
    case ErrorCode::InsufficientMemory:
        return "ERR insufficient_memory requested bytes exceed available capacity\n";
    case ErrorCode::Timeout:
        return "ERR timeout acquire request timed out\n";
    case ErrorCode::InvalidTimeout:
        return "ERR invalid_timeout timeout must be nonnegative milliseconds\n";
    case ErrorCode::None:
        break;
    }
    return "ERR internal_error internal resource manager error\n";
}

std::string format_status(const StatusSnapshot& snapshot) {
    std::ostringstream output;
    output << "OK status " << snapshot.capacity << ' ' << snapshot.used << ' '
           << snapshot.free << ' ' << snapshot.reservations.size() << '\n';
    for (const auto& reservation : snapshot.reservations) {
        output << "CLIENT " << reservation.name << ' ' << reservation.bytes << '\n';
    }
    output << "END\n";
    return output.str();
}

} // namespace gpumemd
