#include "erhe_graphics/glsl_file_loader.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <optional>
#include <sstream>

namespace erhe::graphics {

Glsl_file_loader::Glsl_file_loader(Device& device)
    : m_device{device}
{
}

auto Glsl_file_loader::process_includes(std::size_t source_string_index, const std::string& source) -> std::string
{
    static_cast<void>(source_string_index);
    ERHE_PROFILE_FUNCTION();

    const char* head = source.c_str();

    // Text between includes is appended as whole spans: [span_start, head)
    // is source text that is not in the result yet.
    std::string sb;
    sb.reserve(source.size());
    const char* span_start = head;

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
                            if (log_glsl->should_log(spdlog::level::trace)) {
                                std::stringstream lss;
                                for (std::size_t i = 1, end = m_include_stack.size(); i < end; ++i) {
                                    lss << "    ";
                                }
                                lss << fmt::format("{} includes {}", m_include_stack.back().string(), path.string());
                                log_glsl->trace(lss.str());
                            }

                            sb.append(span_start, include_start);

                            // Place the include directive in comments
                            sb += "//";
                            for (const char* q = include_start; (*q != '\0') && (*q != '\n'); ++q) {
                                if (*q != '\r') {
                                    sb += *q;
                                }
                            }
                            sb += '\n';

                            const std::string included_source = read_shader_source_file(path, m_extra_include_paths);
                            sb += included_source;
                            if (included_source.empty() || included_source.back() != '\n') {
                                sb += '\n';
                            }
                            fmt::format_to(std::back_inserter(sb), "#line {} {}\n", line, source_string_index);

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
                            span_start = head;
                            continue;
                        }
                    }
                }
            }
        }

        if (line_start_white_space_only && !isspace(c)) {
            line_start_white_space_only = false;
        }

        ++head;
    }
    sb.append(span_start, head);
    return sb;
}

auto Glsl_file_loader::read_shader_source_file(
    const std::filesystem::path&              path,
    const std::vector<std::filesystem::path>& extra_include_paths
) -> std::string
{
    m_extra_include_paths = extra_include_paths;
    {
        auto i = std::find(m_include_stack.begin(), m_include_stack.end(), path);
        if (i != m_include_stack.end()) {
            log_glsl->warn("#include cycle for {}", path.string());
            return {};
        }
        if (path.empty()) {
            return {};
        }
    }

    // The path itself, then the same file name under each extra include
    // path. Through the Device's source cache: every variant and stage of a
    // program reads the same few dozen files.
    std::filesystem::path resolved_path = path;
    std::optional<std::string> source;
    {
        ERHE_PROFILE_SCOPE("Glsl_file_loader: find and read file");
        Shader_source_cache& source_cache = m_device.get_shader_source_cache();
        source = source_cache.find_or_read(path);
        if (!source.has_value()) {
            for (const std::filesystem::path& extra : m_extra_include_paths) {
                const std::filesystem::path candidate = extra / path.filename();
                source = source_cache.find_or_read(candidate);
                if (source.has_value()) {
                    resolved_path = candidate;
                    break;
                }
            }
        }
    }
    if (!source.has_value()) {
        // Loud failure: previously this returned a "// Source load
        // failed from: ..." comment string, which downstream code
        // appended to the engine preamble and then handed to
        // glCompileShader. The compile / link then failed far away
        // with a confusing "No definition of main" linker error and
        // no obvious pointer to the missing file. Surface a clear
        // error through the device message callback (so the editor
        // can copy it to the clipboard / show it to the user) and
        // return an empty string so the caller can short-circuit.
        std::stringstream extras;
        for (std::size_t i = 0; i < m_extra_include_paths.size(); ++i) {
            if (i != 0) {
                extras << ", ";
            }
            extras << m_extra_include_paths[i].string();
        }
        m_device.device_message(
            Message_severity::error,
            fmt::format(
                "Could not load shader source: requested '{}', resolved to '{}', extra include paths searched: [{}]",
                path.string(),
                resolved_path.string(),
                extras.str()
            )
        );
        return std::string{};
    }

    m_include_stack.push_back(resolved_path);

    std::size_t source_string_index = 1 + m_source_string_index_to_path.size();
    m_source_string_index_to_path.push_back(resolved_path);
    std::string processed_source = process_includes(source_string_index, source.value());
    std::string result = fmt::format("#line 1 {} // {}\n", source_string_index, resolved_path.string());
    result += processed_source;

    m_include_stack.pop_back();

    return result;
}

auto Glsl_file_loader::get_file_paths() const -> const std::vector<std::filesystem::path>&
{
    return m_source_string_index_to_path;
}

} // namespace erhe::graphics
