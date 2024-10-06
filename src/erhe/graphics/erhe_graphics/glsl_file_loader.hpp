#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace erhe::graphics {

class Glsl_file_loader
{
public:
    [[nodiscard]] auto process_includes(std::size_t source_string_index, const std::string& source) -> std::string;
    [[nodiscard]] auto read_shader_source_file(const std::filesystem::path& path) -> std::string;
    [[nodiscard]] auto get_file_paths() const -> const std::vector<std::filesystem::path>&;

private:
    std::vector<std::filesystem::path> m_source_string_index_to_path;
    std::vector<std::filesystem::path> m_include_stack;
};

} // namespace erhe::graphics
