#include "erhe/toolkit/file.hpp"
#include "erhe/toolkit/log.hpp"
#include "erhe/toolkit/filesystem.hpp"

#include <fstream>

namespace erhe::toolkit
{

auto read(const fs::path& path) -> nonstd::optional<std::string>
{
    // Watch out for fio
    try
    {
        if (
            fs::exists(path) &&
            fs::is_regular_file(path) &&
            !fs::is_empty(path)
        )
        {
            const std::size_t file_length = fs::file_size(path);
            std::FILE* file =
#if defined(_WIN32) // _MSC_VER
                _wfopen(path.c_str(), L"rb");
#else
                std::fopen(path.c_str(), "rb");
#endif
            if (file == nullptr)
            {
                log_file->error("Could not open file '{}' for reading", path.string());
                return {};
            }

            std::size_t bytes_to_read = file_length;
            std::size_t bytes_read = 0;
            std::string result(file_length, '\0');
            do
            {
                const auto read_byte_count = std::fread(result.data() + bytes_read, 1, bytes_to_read, file);
                if (read_byte_count == 0)
                {
                    log_file->error("Error reading file '{}'", path.string());
                    return {};
                }
                bytes_read += read_byte_count;
                bytes_to_read -= read_byte_count;
            }
            while (bytes_to_read > 0);

            std::fclose(file);

            return nonstd::optional<std::string>(result);
        }
    }
    catch (...)
    {
        log_file->error("Error reading file '{}'", path.string());
    }
    return {};
}

} // namespace erhe::toolkit
