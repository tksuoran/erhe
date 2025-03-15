#include "erhe_graphics/glsl_format_source.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

#include <cctype>
#include <optional>
#include <string>
#include <sstream>

namespace erhe::graphics {

auto try_parse_int(char const*& s_in_out) -> std::optional<int>
{
    char const* s = s_in_out;
    if ((s == nullptr) || (*s == '\0')) {
        return std::nullopt;
    }

    int result = 0;
    bool found_digit = false;
    while (true) {
        if ((*s == '\0') || std::isspace(*s)) {
            if (found_digit) {
                s_in_out = s;
                return result;
            } else {
                return std::nullopt;
            }
        }
        if ((*s < '0') || (*s > '9')) {
            return std::nullopt;
        }
        found_digit = true;
        result = result * 10 + (*s - '0');
        ++s;
     }
} 

auto format_source(const std::string& source) -> std::string
{
    ERHE_PROFILE_FUNCTION();

    int         source_string_index{0};
    int         line{1};
    const char* head = source.c_str();
    bool line_start_white_space_only = true;

    std::stringstream sb;
    bool header_pending = true;

    for (;;) {
        char c = *head;
        if (c == '\r') {
            ++head;
            continue;
        }
        if (c == 0) {
            break;
        }

        if (line_start_white_space_only && (c == '#')) {
            const char* line_token = "#line";
            const std::size_t line_token_length = strlen(line_token);
            const int diff = strncmp(head, line_token, line_token_length);
            if (diff == 0) {
                const char* p = head + line_token_length;
                while (*p != '\n' && isspace(*p)) {
                    ++p;
                }
                std::optional<int> line_opt = try_parse_int(p);
                if (line_opt.has_value()) {
                    line = line_opt.value();
                    while (*p != '\n' && isspace(*p)) {
                        ++p;
                    }
                    std::optional<int> source_string_index_opt = try_parse_int(p);
                    if (source_string_index_opt.has_value()) {
                        source_string_index = source_string_index_opt.value();
                    }
                    while (*p != '\n' && *p != '\0') {
                        ++p;
                    }
                    if (*p == '\n') {
                        ++p;
                    }
                    head = p;
                    header_pending = true;
                    continue;
                }
            }
        }

        if (header_pending) {
            sb << fmt::format("{}.{:>3}: ", source_string_index, line);
            header_pending = false;
        }

        sb << c;

        if (c == '\n') {
            ++line;
            line_start_white_space_only = true;
            header_pending = true;
            ++head;
            continue;
        }

        if (line_start_white_space_only && !isspace(c)) {
            line_start_white_space_only = false;
        }
        ++head;
    }
    return sb.str();
}

} // namespace erhe::graphics
