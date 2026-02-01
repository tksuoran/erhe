#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

#include <string>

namespace erhe::graphics {

[[nodiscard]] auto c_str(VkResult result) -> const char*;
[[nodiscard]] auto c_str(VkDebugReportObjectTypeEXT object_type) -> const char*;
[[nodiscard]] auto c_str(VkObjectType object_type) -> const char*;
[[nodiscard]] auto c_str(VkPhysicalDeviceType device_type) -> const char*;
[[nodiscard]] auto c_str(VkDriverId driver_id) -> const char*;
[[nodiscard]] auto c_str(VkFormat format) -> const char*;
[[nodiscard]] auto c_str(VkColorSpaceKHR color_space) -> const char*;
[[nodiscard]] auto c_str(VkPresentModeKHR present_mode) -> const char*;
[[nodiscard]] auto c_str(VkDeviceFaultAddressTypeEXT type) -> const char*;
[[nodiscard]] auto to_string_VkDebugReportFlagsEXT(const VkDebugReportFlagsEXT flags) -> std::string;
[[nodiscard]] auto to_string_VkDebugUtilsMessageSeverityFlagsEXT(const VkDebugUtilsMessageSeverityFlagsEXT severity) -> std::string;
[[nodiscard]] auto to_string_VkDebugUtilsMessageTypeFlagsEXT(const VkDebugUtilsMessageTypeFlagsEXT message_type) -> std::string;
[[nodiscard]] auto to_string_VkPresentScalingFlagsKHR(const VkPresentScalingFlagsKHR present_scaling) -> std::string;
[[nodiscard]] auto to_string_VkPresentGravityFlagsKHR(const VkPresentGravityFlagsKHR present_gravity) -> std::string;

[[nodiscard]] auto to_vulkan(erhe::dataformat::Format format) -> VkFormat;
[[nodiscard]] auto to_erhe  (VkFormat format) -> erhe::dataformat::Format;

[[nodiscard]] auto get_vulkan_sample_count      (int msaa_sample_count) -> VkSampleCountFlagBits;
[[nodiscard]] auto get_vulkan_image_usage_flags (uint64_t usage_mask) -> VkImageUsageFlags;
[[nodiscard]] auto get_vulkan_image_aspect_flags(erhe::dataformat::Format format) -> VkImageAspectFlags;

[[nodiscard]] auto to_vulkan_buffer_usage(Buffer_usage buffer_usage) -> VkBufferUsageFlags;
[[nodiscard]] auto to_vulkan_memory_allocation_create_flags(uint64_t memory_allocation_create_flags) -> VmaAllocationCreateFlags;
[[nodiscard]] auto to_vulkan_memory_property_flags(uint64_t memory_property_flags) -> VkMemoryPropertyFlags;

} // namespace erhe::graphics
