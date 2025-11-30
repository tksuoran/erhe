#pragma once

#include "volk.h"

#include <string>

namespace erhe::graphics {

[[nodiscard]] auto c_str(const VkResult result) -> const char*;
[[nodiscard]] auto c_str(const VkDebugReportObjectTypeEXT object_type) -> const char*;
[[nodiscard]] auto c_str(const VkObjectType object_type) -> const char*;
[[nodiscard]] auto to_string(const VkDebugReportFlagsEXT flags) -> std::string;
[[nodiscard]] auto to_string(const VkDebugUtilsMessageSeverityFlagBitsEXT severity) -> std::string;
[[nodiscard]] auto to_string(const VkDebugUtilsMessageTypeFlagBitsEXT message_type) -> std::string;

} // namespace erhe::graphics
