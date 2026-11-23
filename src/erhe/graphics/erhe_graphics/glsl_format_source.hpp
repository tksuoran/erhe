#pragma once

#include <string>

namespace erhe::graphics {

[[nodiscard]] auto format_source(const std::string& source) -> std::string;

// The source lines around each "string(line)" (NVIDIA) or "string:line" (Mesa) location in info_log.
[[nodiscard]] auto format_info_log_locations(const std::string& source, const std::string& info_log, int context_lines) -> std::string;

}
