#include "erhe_graphics/glsl_format_source.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <set>
#include <utility>
#include <vector>
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

namespace {

class Source_line
{
public:
    int         source_string_index;
    int         line;
    std::string text;
};

// Splits source into lines labelled the way the compiler numbers them after #line directives.
auto split_source_lines(const std::string& source) -> std::vector<Source_line>
{
    std::vector<Source_line> lines;
    int source_string_index{0};
    int line{1};
    std::size_t start = 0;
    while (start <= source.size()) {
        std::size_t end = source.find('\n', start);
        if (end == std::string::npos) {
            end = source.size();
        }
        std::string text = source.substr(start, end - start);
        if (!text.empty() && (text.back() == '\r')) {
            text.pop_back();
        }
        const std::size_t first = text.find_first_not_of(" \t");
        if ((first != std::string::npos) && (text.compare(first, 5, "#line") == 0)) {
            const char* p = text.c_str() + first + 5;
            while ((*p != '\0') && std::isspace(static_cast<unsigned char>(*p))) {
                ++p;
            }
            const std::optional<int> line_opt = try_parse_int(p);
            if (line_opt.has_value()) {
                while ((*p != '\0') && std::isspace(static_cast<unsigned char>(*p))) {
                    ++p;
                }
                const std::optional<int> index_opt = try_parse_int(p);
                if (index_opt.has_value()) {
                    source_string_index = index_opt.value();
                }
                lines.push_back(Source_line{-1, -1, text});
                line = line_opt.value();
                start = end + 1;
                continue;
            }
        }
        lines.push_back(Source_line{source_string_index, line, text});
        ++line;
        if (end == source.size()) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

} // anonymous namespace

auto format_info_log_locations(const std::string& source, const std::string& info_log, const int context_lines) -> std::string
{
    // NVIDIA: "1(1386) : error"; Mesa: "0:12(3): error" / "ERROR: 0:12:"
    static const std::regex nvidia_location{R"((\d+)\((\d+)\)\s*:)"};
    static const std::regex mesa_location  {R"((\d+):(\d+)(\(\d+\))?:)"};

    std::set<std::pair<int, int>> locations;
    for (const std::regex* pattern : {&nvidia_location, &mesa_location}) {
        for (std::sregex_iterator i{info_log.begin(), info_log.end(), *pattern}, end; i != end; ++i) {
            locations.insert({std::stoi((*i)[1].str()), std::stoi((*i)[2].str())});
        }
    }
    if (locations.empty()) {
        return {};
    }

    const std::vector<Source_line> lines = split_source_lines(source);
    std::stringstream sb;
    for (const std::pair<int, int>& location : locations) {
        bool found = false;
        for (std::size_t i = 0, end = lines.size(); i < end; ++i) {
            if ((lines[i].source_string_index != location.first) || (lines[i].line != location.second)) {
                continue;
            }
            found = true;
            sb << fmt::format("Location {}({}) is line {} of the final source:\n", location.first, location.second, i + 1);
            const std::size_t first = (i > static_cast<std::size_t>(context_lines)) ? (i - static_cast<std::size_t>(context_lines)) : 0;
            const std::size_t last  = std::min(end - 1, i + static_cast<std::size_t>(context_lines));
            for (std::size_t j = first; j <= last; ++j) {
                sb << fmt::format("{} {:>6}: {}\n", (j == i) ? ">>" : "  ", j + 1, lines[j].text);
            }
        }
        if (!found) {
            sb << fmt::format("Location {}({}) is not in the final source\n", location.first, location.second);
        }
    }
    return sb.str();
}

} // namespace erhe::graphics
