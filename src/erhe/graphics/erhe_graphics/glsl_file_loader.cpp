#include "erhe_graphics/glsl_file_loader.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace erhe::graphics {

auto Glsl_file_loader::process_includes(std::size_t source_string_index, const std::string& source) -> std::string
{
    static_cast<void>(source_string_index);
    ERHE_PROFILE_FUNCTION();

    const char* head = source.c_str();

    std::stringstream sb;

    bool line_start_white_space_only = true;
    int line{1};
    for (;;) {
        char c = *head;
        if (c == 0) {
            break;
        }

        if (c == '\r' || c == '\n') {
            line += (c == '\n') ? 1 : 0;
            line_start_white_space_only = true;
            sb << c;
            ++head;
            continue;
        }

        if (line_start_white_space_only && (c == '#')) {
            const char* include_start = head;
            const char* include_token = "#include";
            const std::size_t include_token_length = strlen(include_token);
            const int diff = strncmp(head, include_token, include_token_length);
            if (diff == 0) {
                const char* p = head + include_token_length;
                if (isspace(*p)) {
                    while (isspace(*p)) {
                        ++p;
                    }
                    bool open_quotation     = *p == '"';
                    bool open_angle_bracket = *p == '<';
                    if (open_quotation || open_angle_bracket) {
                        ++p;
                        const char* path_start = p;
                        std::size_t path_length = 0;
                        while (
                            (*p != '\0') && 
                            (*p != '\r') && 
                            (*p != '\n') &&
                            (*p != '"') &&
                            (*p != '>')
                        ) {
                            ++p;
                            ++path_length;
                        }
                        bool close_quotation = open_quotation && (*p == '"');
                        bool close_angle_bracket = open_angle_bracket && (*p == '>');
                        if (close_quotation || close_angle_bracket) {
                            std::string filename{path_start, path_length};
                            ERHE_VERIFY(!m_source_string_index_to_path.empty());
                            const std::filesystem::path first = m_source_string_index_to_path.front();
                            const std::filesystem::path directory = first.parent_path();
                            const std::filesystem::path path = directory / filename;

                            // Log include operation
                            std::stringstream lss;
                            for (std::size_t i = 1, end = m_include_stack.size(); i < end; ++i) {
                                lss << "    ";
                            }
                            lss << fmt::format("{} includes {}", m_include_stack.back().string(), path.string());
                            log_glsl->trace(lss.str());

                            // Place the include directive in comments
                            sb << "//";
                            for (const char* q = include_start; (*q != '\0') && (*q != '\n'); ++q) {
                                if (*q != '\r') {
                                    sb << *q;
                                }
                            }
                            sb << "\n";

                            const std::string included_source = read_shader_source_file(path);
                            sb << included_source;
                            if (included_source.empty() || included_source.back() != '\n') {
                                sb << '\n';
                            }
                            sb << "#line " << line << ' ' << source_string_index << '\n';

                            // Drop rest of the line
                            while ((*p != '\0') && (*p != '\n')) {
                                ++p;
                            }
                            if (*p == '\n') {
                                ++p;
                            }

                            // "start" a new line after include
                            ++line;
                            line_start_white_space_only = true;
                            head = p;
                            continue;
                        }
                    }
                }
            }
        }

        if (line_start_white_space_only && !isspace(c)) {
            line_start_white_space_only = false;
        }

        sb << c;
        ++head;
    }
    return sb.str();
}

auto Glsl_file_loader::read_shader_source_file(const std::filesystem::path& path) -> std::string
{
    auto i = std::find(m_include_stack.begin(), m_include_stack.end(), path);
    if (i != m_include_stack.end()) {
        log_glsl->warn("#include cycle for {}", path.string());
        return {};
    }
    if (path.empty()) {
        return {};
    }
    auto source = erhe::file::read("Shader_stages_create_info::final_source", path);
    if (!source.has_value()) {
        return fmt::format("// Source load failed from: {}", path.string());
    }

    m_include_stack.push_back(path);

    std::size_t source_string_index = 1 + m_source_string_index_to_path.size();
    m_source_string_index_to_path.push_back(path);
    std::string processed_source = process_includes(source_string_index, source.value());
    std::stringstream ss;
    ss << "#line 1 " << source_string_index << " // " << path.string() << '\n';
    ss << processed_source;

    m_include_stack.pop_back();

    return ss.str();
}

auto Glsl_file_loader::get_file_paths() const -> const std::vector<std::filesystem::path>&
{
    return m_source_string_index_to_path;
}

} // namespace erhe::graphics
