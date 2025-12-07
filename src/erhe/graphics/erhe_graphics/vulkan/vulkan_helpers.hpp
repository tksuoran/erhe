#pragma once

#include "volk.h"

#include <string>

namespace erhe::graphics {

[[nodiscard]] auto c_str(VkResult result) -> const char*;
[[nodiscard]] auto c_str(VkDebugReportObjectTypeEXT object_type) -> const char*;
[[nodiscard]] auto c_str(VkObjectType object_type) -> const char*;
[[nodiscard]] auto c_str(VkPhysicalDeviceType device_type) -> const char*;
[[nodiscard]] auto c_str(VkDriverId driver_id) -> const char*;
[[nodiscard]] auto c_str(VkFormat format) -> const char*;
[[nodiscard]] auto c_str(VkColorSpaceKHR color_space) -> const char*;
[[nodiscard]] auto c_str(const VkPresentModeKHR present_mode) -> const char*;
[[nodiscard]] auto to_string_VkDebugReportFlagsEXT(const VkDebugReportFlagsEXT flags) -> std::string;
[[nodiscard]] auto to_string_VkDebugUtilsMessageSeverityFlagsEXT(const VkDebugUtilsMessageSeverityFlagsEXT severity) -> std::string;
[[nodiscard]] auto to_string_VkDebugUtilsMessageTypeFlagsEXT(const VkDebugUtilsMessageTypeFlagsEXT message_type) -> std::string;
[[nodiscard]] auto to_string_VkPresentScalingFlagsKHR(const VkPresentScalingFlagsKHR present_scaling) -> std::string;
[[nodiscard]] auto to_string_VkPresentGravityFlagsKHR(const VkPresentGravityFlagsKHR present_gravity) -> std::string;

} // namespace erhe::graphics
