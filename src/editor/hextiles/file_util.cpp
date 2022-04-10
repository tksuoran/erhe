#include "hextiles/file_util.hpp"

#include "erhe/log/log.hpp"

namespace hextiles
{

erhe::log::Category log_file{0.6f, 1.0f, 0.6f, erhe::log::Console_color::GREEN, erhe::log::Level::LEVEL_INFO};

auto read_file(const char* const path) -> std::vector<unsigned char>
{
    if (path == nullptr)
    {
        log_file.error("read_file(): path = nullptr\n");
        return {};
    }

    FILE* const file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        log_file.error("Failed to open '{}'\n", path);
        return {};
    }

    std::fseek(file, 0L, SEEK_END);
    const long file_length = std::ftell(file);
    std::rewind(file);
    std::vector<unsigned char> data(file_length);
    std::fread(data.data(), file_length, 1, file);
    std::fclose(file);
    return data;
}

auto read_file_string(const char* const path) -> std::string
{
    if (path == nullptr)
    {
        log_file.error("read_file(): path = nullptr\n");
        return {};
    }

    FILE* const file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        log_file.error("Failed to open '{}'\n", path);
        return {};
    }
    std::fseek(file, 0L, SEEK_END);
    const long file_length = std::ftell(file);
    std::rewind(file);
    std::vector<unsigned char> data(file_length);
    std::fread(data.data(), file_length, 1, file);
    std::fclose(file);
    return std::string(data.begin(), data.end());
}

auto write_file(
    const char* const    path,
    const unsigned char* buffer,
    const size_t         length
) -> bool
{
    if (path == nullptr)
    {
        log_file.error("write_file(): path = nullptr\n");
        return false;
    }

    FILE* const file = std::fopen(path, "wb");
    if (file == nullptr)
    {
        log_file.error("Failed to open '{}'\n", path);
        return false;
    }
    const size_t res = std::fwrite(buffer, 1, length, file);
    if (res != length)
    {
        log_file.error("Failed to write '{}'\n", path);
        return false;
    }
    std::fclose(file);
    return true;
}

auto write_file(
    const char* const  path,
    const std::string& text
) -> bool
{
    if (path == nullptr)
    {
        log_file.error("write_file(): path = nullptr\n");
        return false;
    }

    FILE* const file = std::fopen(path, "wb");
    if (file == nullptr)
    {
        log_file.error("Failed to open '{}'\n", path);
        return false;
    }
    const size_t res = std::fwrite(text.data(), 1, text.size(), file);
    if (res != text.size())
    {
        log_file.error("Failed to write '{}'\n", path);
        return false;
    }
    std::fclose(file);
    return true;
}

auto get_string(
    const std::vector<unsigned char>& data,
    const size_t                      offset,
    const size_t                      length
) -> std::string
{
    std::string result(length, 0);
    for (size_t pos = 0U; pos < length; pos++)
    {
        result[pos] = static_cast<char>(data[offset + pos]);
    }
    return result;
}

}