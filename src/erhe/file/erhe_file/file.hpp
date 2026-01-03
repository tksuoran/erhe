#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace erhe::file {

auto to_string(const std::filesystem::path& path) -> std::string;

auto from_string(const std::string& path) -> std::filesystem::path;

// return value will be empty if file does not exist, or is not regular file, or is empty
[[nodiscard]] auto read(const std::string_view description, const std::filesystem::path& path) -> std::optional<std::string>;

auto write_file(std::filesystem::path path, const std::string& text) -> bool;

// TODO open, save, ...
[[nodiscard]] auto select_file_for_read() -> std::optional<std::filesystem::path>;
[[nodiscard]] auto select_file_for_write() -> std::optional<std::filesystem::path>;

[[nodiscard]] auto check_is_existing_non_empty_regular_file(
    const std::string_view       description,
    const std::filesystem::path& path,
    bool                         silent_if_not_exists = false
) -> bool;

void ensure_working_directory_contains(const char* target);

} // namespace erhe::file
