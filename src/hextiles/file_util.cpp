#include "file_util.hpp"

#include "hextiles_log.hpp"

namespace hextiles
{

auto read_file(const char* const path) -> std::vector<unsigned char>
{
    if (path == nullptr) {
        log_file->error("read_file(): path = nullptr");
        return {};
    }

    FILE* const file = std::fopen(path, "rb");
    if (file == nullptr) {
        log_file->error("Failed to open '{}'", path);
        return {};
    }

    std::fseek(file, 0L, SEEK_END);
    const long file_length = std::ftell(file);
    std::rewind(file);
    std::vector<unsigned char> data(file_length);
    const auto read_count = std::fread(data.data(), 1, file_length, file);
    if (read_count != file_length) {
        log_file->warn("Failed to read all {} bytes of '{}', only read {} bytes", file_length, path, read_count);
    }
    std::fclose(file);
    return data;
}

auto read_file_string(const char* const path) -> std::string
{
    if (path == nullptr) {
        log_file->error("read_file(): path = nullptr");
        return {};
    }

    FILE* const file = std::fopen(path, "rb");
    if (file == nullptr) {
        log_file->error("Failed to open '{}' for reading", path);
        return {};
    }
    std::fseek(file, 0L, SEEK_END);
    const long file_length = std::ftell(file);
    std::rewind(file);
    std::vector<unsigned char> data(file_length);
    const auto read_count = std::fread(data.data(), 1, file_length, file);
    if (read_count != file_length) {
        log_file->warn("Failed to read all {} bytes of '{}', only read {} bytes", file_length, path, read_count);
    }
    std::fclose(file);
    return std::string(data.begin(), data.end());
}

auto write_file(
    const char* const    path,
    const unsigned char* buffer,
    const size_t         length
) -> bool
{
    if (path == nullptr) {
        log_file->error("write_file(): path = nullptr");
        return false;
    }

    FILE* const file = std::fopen(path, "wb");
    if (file == nullptr) {
        log_file->error("Failed to open '{}' for writing", path);
        return false;
    }
    const size_t res = std::fwrite(buffer, 1, length, file);
    std::fclose(file);
    if (res != length) {
        log_file->error("Failed to write '{}'", path);
        return false;
    }
    return true;
}

auto write_file(
    const char* const  path,
    const std::string& text
) -> bool
{
    if (path == nullptr) {
        log_file->error("write_file(): path = nullptr");
        return false;
    }

    FILE* const file = std::fopen(path, "wb");
    if (file == nullptr) {
        log_file->error("Failed to open '{}' for writing", path);
        return false;
    }
    const size_t res = std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    if (res != text.size()) {
        log_file->error("Failed to write '{}'", path);
        return false;
    }
    return true;
}

auto get_string(
    const std::vector<unsigned char>& data,
    const size_t                      offset,
    const size_t                      length
) -> std::string
{
    std::string result(length, 0);
    for (size_t pos = 0U; pos < length; pos++) {
        result[pos] = static_cast<char>(data[offset + pos]);
    }
    return result;
}

}
