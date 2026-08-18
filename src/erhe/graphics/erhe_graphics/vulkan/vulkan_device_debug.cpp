#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <cstring>
#include <vector>

namespace erhe::graphics {

namespace {

// vkGetDeviceFaultReportsKHR takes a timeout to wait for a report to become
// available. We are already on a fatal path when this runs, so a short block
// is cheaper than losing the only explanation of the crash.
constexpr uint64_t c_device_fault_report_timeout_ns = 1'000'000'000ull;

// The driver-supplied strings are fixed-size char arrays that are only
// guaranteed NUL-terminated when the driver filled them in.
[[nodiscard]] auto bounded(const char* text, const std::size_t max_size) -> std::string
{
    const std::size_t length = ::strnlen(text, max_size);
    return std::string{text, length};
}

void log_fault_address_info(const char* what, const VkDeviceFaultAddressInfoKHR& address_info)
{
    if (address_info.addressType == VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_KHR) {
        return;
    }
    // addressPrecision is a power of two: the reported address is only known
    // to lie within [reportedAddress & ~(precision - 1), + precision).
    const VkDeviceSize    precision = address_info.addressPrecision;
    const VkDeviceAddress lower     = (precision != 0) ? (address_info.reportedAddress & ~(precision - 1)) : address_info.reportedAddress;
    const VkDeviceAddress upper     = (precision != 0) ? (lower + precision - 1) : address_info.reportedAddress;
    log_context->critical(
        "  {}: {} address 0x{:x} precision 0x{:x} (range 0x{:x} .. 0x{:x})",
        what, c_str(address_info.addressType), address_info.reportedAddress, precision, lower, upper
    );
}

void log_fault_vendor_info(const VkDeviceFaultVendorInfoKHR& vendor_info)
{
    log_context->critical(
        "  vendor fault: code 0x{:x} data 0x{:x} '{}'",
        vendor_info.vendorFaultCode, vendor_info.vendorFaultData,
        bounded(vendor_info.description, VK_MAX_DESCRIPTION_SIZE)
    );
}

} // anonymous namespace

// VK_KHR_device_fault: one call can return several reports, each already
// carrying its own fault/instruction address and vendor info.
void Device_impl::report_device_fault_khr()
{
    if (vkGetDeviceFaultReportsKHR == nullptr) {
        return;
    }
    uint32_t report_count = 0;
    VkResult result = vkGetDeviceFaultReportsKHR(m_vulkan_device, c_device_fault_report_timeout_ns, &report_count, nullptr);
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) {
        log_context->critical("vkGetDeviceFaultReportsKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        return;
    }
    if (report_count == 0) {
        log_context->critical("VK_KHR_device_fault: driver reported no faults");
        return;
    }
    std::vector<VkDeviceFaultInfoKHR> reports(report_count);
    for (VkDeviceFaultInfoKHR& report : reports) {
        report.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR;
        report.pNext = nullptr;
    }
    result = vkGetDeviceFaultReportsKHR(m_vulkan_device, c_device_fault_report_timeout_ns, &report_count, reports.data());
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) {
        log_context->critical("vkGetDeviceFaultReportsKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        return;
    }
    reports.resize(report_count);
    for (std::size_t i = 0, end = reports.size(); i < end; ++i) {
        const VkDeviceFaultInfoKHR& report = reports[i];
        log_context->critical(
            "VK_KHR_device_fault report {}/{}: flags {} group {} '{}'",
            i + 1, end, to_string_VkDeviceFaultFlagsKHR(report.flags), report.groupId,
            bounded(report.description, VK_MAX_DESCRIPTION_SIZE)
        );
        log_fault_address_info("fault",       report.faultAddressInfo);
        log_fault_address_info("instruction", report.instructionAddressInfo);
        if ((report.flags & VK_DEVICE_FAULT_FLAG_VENDOR_KHR) != 0) {
            log_fault_vendor_info(report.vendorInfo);
        }
    }
    if (result == VK_INCOMPLETE) {
        log_context->critical("VK_KHR_device_fault: more reports are available than were retrieved");
    }
}

// VK_EXT_device_fault: a single report per call, with the address and vendor
// info in caller-allocated arrays sized by a first counts-only call.
void Device_impl::report_device_fault_ext()
{
    if (vkGetDeviceFaultInfoEXT == nullptr) {
        return;
    }
    VkDeviceFaultCountsEXT counts{
        .sType            = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT,
        .pNext            = nullptr,
        .addressInfoCount = 0,
        .vendorInfoCount  = 0,
        .vendorBinarySize = 0
    };
    VkResult result = vkGetDeviceFaultInfoEXT(m_vulkan_device, &counts, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkGetDeviceFaultInfoEXT() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        return;
    }
    std::vector<VkDeviceFaultAddressInfoEXT> address_infos(counts.addressInfoCount);
    std::vector<VkDeviceFaultVendorInfoEXT>  vendor_infos (counts.vendorInfoCount);
    // deviceFaultVendorBinary was not enabled, so never ask for the blob.
    counts.vendorBinarySize = 0;
    VkDeviceFaultInfoEXT info{
        .sType             = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
        .pNext             = nullptr,
        .description       = {},
        .pAddressInfos     = address_infos.empty() ? nullptr : address_infos.data(),
        .pVendorInfos      = vendor_infos.empty()  ? nullptr : vendor_infos.data(),
        .pVendorBinaryData = nullptr
    };
    result = vkGetDeviceFaultInfoEXT(m_vulkan_device, &counts, &info);
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) {
        log_context->critical("vkGetDeviceFaultInfoEXT() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        return;
    }
    log_context->critical(
        "VK_EXT_device_fault report: '{}' ({} address info(s), {} vendor info(s))",
        bounded(info.description, VK_MAX_DESCRIPTION_SIZE), counts.addressInfoCount, counts.vendorInfoCount
    );
    address_infos.resize(counts.addressInfoCount);
    vendor_infos.resize(counts.vendorInfoCount);
    for (const VkDeviceFaultAddressInfoEXT& address_info : address_infos) {
        log_fault_address_info("fault", address_info);
    }
    for (const VkDeviceFaultVendorInfoEXT& vendor_info : vendor_infos) {
        log_fault_vendor_info(vendor_info);
    }
    if (result == VK_INCOMPLETE) {
        log_context->critical("VK_EXT_device_fault: report was truncated");
    }
}

void Device_impl::report_device_fault(const char* site)
{
    if (m_vulkan_device == VK_NULL_HANDLE) {
        return;
    }
    if (!has_device_fault_report()) {
        // Say so rather than staying silent: a crash log that shows a bare
        // VK_ERROR_DEVICE_LOST and nothing else is indistinguishable from one
        // where the report was requested and came back empty. Capture layers
        // are the common reason - RenderDoc's capture layer only forwards the
        // device extensions it implements, so VK_EXT_device_fault disappears
        // from the physical device's list while it is loaded.
        log_context->critical(
            "Device fault report requested at {} but neither VK_KHR_device_fault nor VK_EXT_device_fault is enabled "
            "(see the 'deviceFault' line in the startup log; capture layers can filter the extension away)",
            (site != nullptr) ? site : "?"
        );
        return;
    }
    log_context->critical("Device fault report requested at {}", (site != nullptr) ? site : "?");
    if (m_device_fault_report_khr) {
        report_device_fault_khr();
    } else {
        report_device_fault_ext();
    }
}

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
    log_debug ->log(level, ss.str());
    ERHE_VULKAN_SYNC_LOG(level, ss.str());
    log_vulkan->log(level, ss.str());
    return VK_TRUE;
}

auto Device_impl::debug_utils_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
    VkDebugUtilsMessageTypeFlagsEXT             message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data
) -> VkBool32
{
    if (callback_data->messageIdNumber == 0xe19c8e05) {
        // BestPractices-Verbose-Success-Logging
        return VK_FALSE;
    }
    if (callback_data->messageIdNumber == 0xde900250) {
        // https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/issues/530
        // vkGetPhysicalDeviceMemoryProperties(): vkGetPhysicalDeviceMemoryProperties is a legacy command and this VkInstance was created with VK_VERSION_1_1 which contains vkGetPhysicalDeviceMemoryProperties2 that can be used instead.
        return VK_FALSE;
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
        "{} {} [{:08x} {}] {}",
        to_string_VkDebugUtilsMessageTypeFlagsEXT(message_types),
        to_string_VkDebugUtilsMessageSeverityFlagsEXT(message_severity),
        static_cast<uint32_t>(callback_data->messageIdNumber),
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
    // Also mirror into the unified vulkan.txt sink so validation messages
    // interleave chronologically with [FRAME_BEGIN]/[RP_BEGIN]/[DRAW]/etc.
    ERHE_VULKAN_SYNC_LOG(level, ss.str());
    log_vulkan->log(level, ss.str());
    if (callback_data->messageIdNumber == 0) {
        return VK_FALSE;
    }


    Message_severity severity = Message_severity::info;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0) severity = Message_severity::verbose;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT   ) != 0) severity = Message_severity::info;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) severity = Message_severity::warning;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT  ) != 0) severity = Message_severity::error;

    // We trigger this when we intentionally clear textures with 1,0,1,1
    // to see if we miss rendering and end up seeing clear color.

    // [916108d1 BestPractices-NVIDIA-ClearColor-NotCompressed] vkCmdClearColorImage(): [NVIDIA]
    // Clearing image with format VK_FORMAT_R8G8B8A8_SRGB without a 1.0f or 0.0f clear color.
    // The clear will not get compressed in the GPU, harming performance.
    // This can be fixed using a clear color of VkClearColorValue{0.0f, 0.0f, 0.0f, 0.0f},
    // or VkClearColorValue{1.0f, 1.0f, 1.0f, 1.0f}.
    // Alternatively, use VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
    // VK_FORMAT_A8B8G8R8_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32,
    // VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_R16G16B16A16_UNORM,
    // VK_FORMAT_R16G16B16A16_SNORM, VK_FORMAT_R16G16B16A16_UINT, VK_FORMAT_R16G16B16A16_SINT,
    // VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT, or
    // VK_FORMAT_B10G11R11_UFLOAT_PACK32.
    //if (callback_data->messageIdNumber == 0x916108d1) {
    //    severity = Message_severity::error;
    //    static int counter = 0;
    //    ++counter;
    //}
    m_device.device_message(severity, ss.str());

    return VK_FALSE;
}

}
