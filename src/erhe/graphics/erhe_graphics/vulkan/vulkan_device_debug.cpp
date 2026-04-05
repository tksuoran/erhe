#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

auto Device_impl_debug_report_callback(
    VkDebugReportFlagsEXT      flags,
    VkDebugReportObjectTypeEXT object_type,
    uint64_t                   object,
    size_t                     location,
    int32_t                    message_code,
    const char*                layer_prefix,
    const char*                message,
    void*                      user_data
) -> VkBool32
{
    Device_impl* device_impl = static_cast<Device_impl*>(user_data);
    if (device_impl == nullptr) {
        return VK_FALSE;
    }
    return device_impl->debug_report_callback(
        flags,
        object_type,
        object,
        location,
        message_code,
        layer_prefix,
        message
    );
}

[[nodiscard]] auto Device_impl_debug_utils_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
    VkDebugUtilsMessageTypeFlagsEXT             message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*                                       user_data
) -> VkBool32
{
    Device_impl* device_impl = static_cast<Device_impl*>(user_data);
    if (device_impl == nullptr) {
        return VK_FALSE;
    }
    return device_impl->debug_utils_messenger_callback(message_severity, message_types, callback_data);
}

auto Device_impl::debug_report_callback(
    VkDebugReportFlagsEXT      flags,
    VkDebugReportObjectTypeEXT object_type,
    uint64_t                   object,
    size_t                     location,
    int32_t                    message_code,
    const char*                layer_prefix,
    const char*                message
) -> VkBool32
{
    spdlog::level::level_enum level = spdlog::level::info;
    if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT              ) level = spdlog::level::trace;
    if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT        ) level = spdlog::level::info;
    if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) level = spdlog::level::info;
    if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT            ) level = spdlog::level::warn;
    if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT              ) level = spdlog::level::err;
    std::stringstream ss;
    if (layer_prefix != nullptr) {
        ss << "[" << layer_prefix << "] ";
    }
    ss << fmt::format(
        "[{:<24}] Object {:16} (type {}) Location {:08x} Code {:04} :",
        to_string_VkDebugReportFlagsEXT(flags),
        object,
        c_str(object_type),
        location,
        message_code
    );
    if (message != nullptr) {
        ss << " " << message;
    }  
    log_debug->log(level, ss.str());
    return VK_TRUE;
}

auto Device_impl::debug_utils_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
    VkDebugUtilsMessageTypeFlagsEXT             message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data
) -> VkBool32
{
    if (callback_data->messageIdNumber == 0x675dc32e) {
        // Validation Warning: [ BestPractices-specialuse-extension ] | MessageID = 0x675dc32e
        // vkCreateInstance(): Attempting to enable extension VK_EXT_debug_utils, but this extension is intended to support use by applications when debugging and it is strongly recommended that it be otherwise avoided.
    }

    spdlog::level::level_enum level = spdlog::level::info;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0) level = spdlog::level::trace;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT   ) != 0) level = spdlog::level::info;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) level = spdlog::level::warn;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT  ) != 0) level = spdlog::level::err;
    std::stringstream ss;
    bool show_objects = true;
    if (callback_data->messageIdNumber == 0) {
        if (
            (callback_data->pMessageIdName != nullptr) &&
            strcmp(callback_data->pMessageIdName, "Loader Message") == 0
        ) {
            show_objects = false;
        }
    }
    ss << fmt::format(
        "{} {} [{} {}] {}",
        to_string_VkDebugUtilsMessageTypeFlagsEXT(message_types),
        to_string_VkDebugUtilsMessageSeverityFlagsEXT(message_severity),
        callback_data->messageIdNumber,
        (callback_data->pMessageIdName != nullptr) ? callback_data->pMessageIdName : "",
        (callback_data->pMessage != nullptr) ? callback_data->pMessage : "<no message>"
    );
    if (callback_data->queueLabelCount > 0) {
        ss << "\n  queues: ";
        bool empty = true;
        for (uint32_t i = 0; i < callback_data->queueLabelCount; ++i) {
            if (callback_data->pQueueLabels[i].pLabelName != nullptr) {
                if (!empty) {
                    ss << ", ";
                }
                ss << callback_data->pQueueLabels[i].pLabelName;
                empty = false;
            }
        }
    }
    if (callback_data->cmdBufLabelCount > 0) {
        ss << "\n  command buffers: ";
        bool empty = true;
        for (uint32_t i = 0; i < callback_data->cmdBufLabelCount; ++i) {
            if (callback_data->pCmdBufLabels[i].pLabelName != nullptr) {
                if (!empty) {
                    ss << ", ";
                }
                ss << callback_data->pCmdBufLabels[i].pLabelName;
                empty = false;
            }
        }
    }
    if (show_objects && (callback_data->objectCount > 0)) {
        ss << "\n  objects: ";
        bool empty = true;
        for (uint32_t i = 0; i < callback_data->objectCount; ++i) {
            if (!empty) {
                ss << ", ";
            }
            ss << fmt::format(
                "{} {:08x} {}",
                c_str(callback_data->pObjects[i].objectType),
                callback_data->pObjects[i].objectHandle, 
                (callback_data->pObjects[i].pObjectName != nullptr)
                    ? callback_data->pObjects[i].pObjectName
                    : ""
            );
            empty = false;
        }
    }
    log_debug->log(level, ss.str());

    const bool is_validation = (message_types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0;
    if (is_validation) {
        const bool is_warning = (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0;
        const bool is_error   = (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT  ) != 0;
        if (is_warning || is_error) {
            m_device.device_error(ss.str());
        }
        return (is_warning || is_error) ? VK_TRUE : VK_FALSE;
    }

    return VK_TRUE;
}

}
