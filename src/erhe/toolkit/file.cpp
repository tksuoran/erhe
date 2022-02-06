#include "erhe/toolkit/file.hpp"
#include "erhe/log/log.hpp"
#include "erhe/toolkit/filesystem.hpp"

#include <fstream>

namespace erhe::toolkit
{

using Category      = erhe::log::Category;
using Console_color = erhe::log::Console_color;
using Level         = erhe::log::Level;

Category log{1.0f, 1.0f, 0.5f, Console_color::YELLOW, Level::LEVEL_INFO};

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
            const size_t file_length = fs::file_size(path);
            std::FILE* file =
#ifdef _MSC_VER
                _wfopen(path.c_str(), L"rb");
#else
                std::fopen(path.c_str(), "rb");
#endif
            if (file == nullptr)
            {
                log.error("Could not open file {} for reading\n", path.string());
                return {};
            }

            size_t bytes_to_read = file_length;
            size_t bytes_read = 0;
            std::string result(file_length, '\0');
            do
            {
                const auto read_byte_count = std::fread(result.data() + bytes_read, 1, bytes_to_read, file);
                if (read_byte_count == 0)
                {
                    log.error("Error reading file {}\n", path.string());
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
        log.error("Error reading file {}\n", path.string());
    }
    return {};
}

} // namespace erhe::toolkit
