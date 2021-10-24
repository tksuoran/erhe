#include "erhe/toolkit/file.hpp"

#include <filesystem>
#include <fstream>

namespace erhe::toolkit
{

auto read(const std::filesystem::path& path) -> std::optional<std::string>
{
    // Watch out for fio
    try
    {
        if (std::filesystem::exists(path) &&
            std::filesystem::is_regular_file(path) &&
            !std::filesystem::is_empty(path))
        {
            const size_t file_length = std::filesystem::file_size(path);
            std::string result(file_length, '\0');
            std::ifstream stream(path, std::ios::binary);
            stream.read(result.data(), file_length);
            return std::optional<std::string>(result);
        }
    }
    catch (...)
    {
    }
    return {};
}

} // namespace erhe::toolkit
