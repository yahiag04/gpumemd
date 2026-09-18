#include "gpumemd/model_loader.hpp"

#include <filesystem>
#include <fstream>
#include <limits>

namespace gpumemd {

ModelLoadResult ModelLoader::load(std::string_view path, Bytes expected_bytes) const {
    if (path.empty() || expected_bytes == 0) {
        return {ModelLoadError::Unavailable, {}};
    }

    const std::filesystem::path file_path(path);
    std::error_code status_error;
    if (!std::filesystem::is_regular_file(file_path, status_error) || status_error) {
        return {ModelLoadError::Unavailable, {}};
    }

    const auto file_size = std::filesystem::file_size(file_path, status_error);
    if (status_error || file_size != expected_bytes ||
        file_size > std::numeric_limits<std::size_t>::max()) {
        return {status_error ? ModelLoadError::Unavailable : ModelLoadError::SourceChanged,
                {}};
    }

    std::vector<std::byte> data(static_cast<std::size_t>(file_size));
    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        return {ModelLoadError::ReadFailure, {}};
    }
    input.read(reinterpret_cast<char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(data.size())) {
        return {ModelLoadError::ReadFailure, {}};
    }
    return {ModelLoadError::None, std::move(data)};
}

} // namespace gpumemd
