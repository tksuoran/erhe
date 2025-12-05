// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_window/window.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

#include <sstream>
#include <vector>

namespace erhe::graphics {

// https://gist.github.com/nanokatze/bb03a486571e13a7b6a8709368bd87cf#handling-window-resize
// https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/api/swapchain_recreation

Surface_impl::Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info)
    : m_device_impl        {device_impl}
    , m_surface_create_info{create_info}
    , m_surface_format{
        .format     = VK_FORMAT_UNDEFINED,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    }
    , m_present_mode      {VK_PRESENT_MODE_FIFO_KHR}
{
    ERHE_VERIFY(create_info.context_window != nullptr);
    const VkInstance vulkan_instance = device_impl.get_vulkan_instance();
    void* surface = create_info.context_window->create_vulkan_surface(static_cast<void*>(vulkan_instance));
    m_surface = static_cast<VkSurfaceKHR>(surface);
}

auto Surface_impl::can_use_physical_device(VkPhysicalDevice physical_device) -> bool
{
    if (m_surface == VK_NULL_HANDLE) {
        fail();
        return false;
    }

    VkSurfaceCapabilitiesKHR surface_capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, m_surface, &surface_capabilities);
    if (result != VK_SUCCESS) {
        fail();
        return false;
    }

    uint32_t format_count{0};
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, m_surface, &format_count, nullptr);
    if (result != VK_SUCCESS) {
        fail();
        return false;
    }
    if (format_count == 0) {
        fail();
        return false;
    }
    m_surface_formats.resize(format_count);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, m_surface, &format_count, m_surface_formats.data());
    if (result != VK_SUCCESS) {
        fail();
        return false;
    }

    uint32_t present_mode_count;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_surface, &present_mode_count, nullptr);
    if (result != VK_SUCCESS) {
        fail();
        return false;
    }
    if (present_mode_count == 0) {
        fail();
        return false;
    }

    m_present_modes.resize(present_mode_count);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_surface, &present_mode_count, m_present_modes.data());
    if (result != VK_SUCCESS) {
        fail();
        return false;
    }

    return true;
}

auto Surface_impl::use_physical_device(VkPhysicalDevice physical_device) -> bool
{
    if (m_surface == VK_NULL_HANDLE) {
        fail();
        return false;
    }

    VkSurfaceCapabilitiesKHR surface_capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, m_surface, &surface_capabilities);
    if (result != VK_SUCCESS) {
        fail();
        return false;
    }

    uint32_t format_count{0};
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, m_surface, &format_count, nullptr);
    if (result != VK_SUCCESS) {
        fail();
        return false;
    }
    if (format_count == 0) {
        fail();
        return false;
    }
    m_surface_formats.resize(format_count);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, m_surface, &format_count, m_surface_formats.data());
    if (result != VK_SUCCESS) {
        fail();
        return false;
    }

    choose_surface_format();

    uint32_t present_mode_count;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_surface, &present_mode_count, nullptr);
    if (result != VK_SUCCESS) {
        fail();
        return false;
    }
    if (present_mode_count == 0) {
        fail();
        return false;
    }

    m_present_modes.resize(present_mode_count);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_surface, &present_mode_count, m_present_modes.data());
    if (result != VK_SUCCESS) {
        fail();
        return false;
    }

    choose_present_mode();

    m_physical_device = physical_device;

    m_image_count = 0;
    switch (m_present_mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR: {
            m_image_count = 2; // swap on vsync or tear-swap if late
            break;
        }
        case VK_PRESENT_MODE_MAILBOX_KHR:
        case VK_PRESENT_MODE_FIFO_KHR: {
            m_image_count = 3; // one displayed, one queued, one rendering
            break;
        }
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: {
            m_image_count = 2; // Vsync when on time, immediate with tearing when late
            break;
        }
        case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:
        case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: {
            m_image_count = 0;
            break;
        }
        case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR: {
            m_image_count = 3;
            break;
        }
        default: {
            m_image_count = 0;
            break;
        }
    }
    m_image_count = std::max(m_image_count, surface_capabilities.minImageCount);
    if (surface_capabilities.maxImageCount != 0) {
        m_image_count = std::min(
            m_image_count,
            surface_capabilities.maxImageCount
        );
    }

    return true;
}

void Surface_impl::fail()
{
    m_physical_device = VK_NULL_HANDLE;
    m_surface_formats.clear();
    m_present_modes.clear();
    m_surface_format = {
        .format     = VK_FORMAT_UNDEFINED,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    };
    m_present_mode = VK_PRESENT_MODE_FIFO_KHR;
    m_image_count = 0;
}

auto Surface_impl::get_surface_format_score(const VkSurfaceFormatKHR surface_format) const -> float
{
    float format_score = 1.0f;
    switch (surface_format.format) {
        case VK_FORMAT_R8G8B8A8_UNORM:           format_score = 2.0f; break;
        case VK_FORMAT_B8G8R8A8_UNORM:           format_score = 2.0f; break;
        case VK_FORMAT_R8G8B8A8_SRGB:            format_score = 3.0f; break;
        case VK_FORMAT_B8G8R8A8_SRGB:            format_score = 3.0f; break;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: format_score = 4.0f; break;
        default:
            break;
    }

    float color_space_score = 10.0f;
    switch (surface_format.colorSpace) {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:       color_space_score = 2.0f; break;
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
        case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:
        case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT:
        case VK_COLOR_SPACE_BT709_LINEAR_EXT:
        case VK_COLOR_SPACE_BT709_NONLINEAR_EXT:
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
        case VK_COLOR_SPACE_HDR10_ST2084_EXT:
        case VK_COLOR_SPACE_DOLBYVISION_EXT:
        case VK_COLOR_SPACE_HDR10_HLG_EXT:
        case VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT:
        case VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT:
        case VK_COLOR_SPACE_PASS_THROUGH_EXT:
        case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT:
        case VK_COLOR_SPACE_DISPLAY_NATIVE_AMD:
        default:
            break;
    }

    return format_score * color_space_score;
}

auto Surface_impl::get_present_mode_score(const VkPresentModeKHR present_mode) const -> float
{
    // https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#VkSurfacePresentModeKHR

    // VK_PRESENT_MODE_IMMEDIATE_KHR specifies that
    //   the presentation engine does not wait for a vertical blanking period to update the current image,
    //   meaning this mode may result in visible tearing.
    //   No internal queuing of presentation requests is needed, as the requests are applied immediately.

    // VK_PRESENT_MODE_MAILBOX_KHR specifies that
    //   the presentation engine waits for the next vertical blanking period to update the current image.
    //   Tearing cannot be observed. An internal single-entry queue is used to hold pending presentation requests.
    //   If the queue is full when a new presentation request is received, the new request replaces the existing
    //   entry, and any images associated with the prior entry become available for reuse by the application.
    //   One request is removed from the queue and processed during each vertical blanking period in which the
    //   queue is non-empty.

    // VK_PRESENT_MODE_FIFO_KHR specifies that the
    //   presentation engine waits for the next vertical blanking period to update the current image.
    //   Tearing cannot be observed.
    //   An internal queue is used to hold pending presentation requests.
    //   New requests are appended to the end of the queue, and one request is removed from the beginning of the
    //   queue and processed during each vertical blanking period in which the queue is non-empty.
    //   This is the only value of presentMode that is required to be supported.

    // VK_PRESENT_MODE_FIFO_RELAXED_KHR specifies that
    //   the presentation engine generally waits for the next vertical blanking period to update the current image.
    //   If a vertical blanking period has already passed since the last update of the current image then the
    //   presentation engine does not wait for another vertical blanking period for the update, meaning this mode
    //   may result in visible tearing in this case.
    //   This mode is useful for reducing visual stutter with an application that will mostly present a new image
    //   before the next vertical blanking period, but may occasionally be late, and present a new image just after
    //   the next vertical blanking period.
    //   An internal queue is used to hold pending presentation requests.
    //   New requests are appended to the end of the queue, and one request is removed from the beginning of the
    //   queue and processed during or after each vertical blanking period in which the queue is non-empty.

    // VK_PRESENT_MODE_FIFO_LATEST_READY_KHR specifies that
    //   the presentation engine waits for the next vertical blanking period to update the current image.
    //   Tearing cannot be observed.
    //   An internal queue is used to hold pending presentation requests.
    //   New requests are appended to the end of the queue.
    //   At each vertical blanking period, the presentation engine dequeues all successive requests that are
    //   ready to be presented from the beginning of the queue.
    //   If using VK_GOOGLE_display_timing to provide a target present time, the presentation engine will check
    //   the specified time for each image.
    //   If the target present time is less-than or equal-to the current time, the presentation engine will
    //   dequeue the image and check the next one.
    //   The image of the last dequeued request will be presented.
    //   The other dequeued requests will be dropped.

    // VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR specifies that
    //   the presentation engine and application have concurrent access to a single image, which is referred to
    //   as a shared presentable image.
    //   The presentation engine is only required to update the current image after a new presentation request
    //   is received.
    //   Therefore the application must make a presentation request whenever an update is required.
    //   However, the presentation engine may update the current image at any point, meaning this mode may result
    //   in visible tearing.

    // VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR specifies that
    //   the presentation engine and application have concurrent access to a single image, which is referred to
    //   as a shared presentable image.
    //   The presentation engine periodically updates the current image on its regular refresh cycle.
    //   The application is only required to make one initial presentation request, after which the presentation
    //   engine must update the current image without any need for further presentation requests.
    //   The application can indicate the image contents have been updated by making a presentation request, but
    //   this does not guarantee the timing of when it will be updated.
    //   This mode may result in visible tearing if rendering to the image is not timed correctly.

    // TODO Disabled for now, because it drops frames and that makes initial
    //      swapchain bringup a bit more unintuitive.
    //const bool latest_ready =
    //    m_device_impl.get_device_extensions().m_VK_KHR_present_mode_fifo_latest_ready ||
    //    m_device_impl.get_device_extensions().m_VK_EXT_present_mode_fifo_latest_ready;

    switch (present_mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:                 return 0.0f; // fastest - tears
        case VK_PRESENT_MODE_MAILBOX_KHR:                   return 3.0f; // latest complete shown - no tears
        case VK_PRESENT_MODE_FIFO_KHR:                      return 1.0f; // classic vsync - no tears
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:              return 2.0f; // late frames presented immediately - some tears
        case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:     return 0.0f;
        case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: return 0.0f;
        // TODO Need config to allow/disallow dropping frames
        //case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR:         return latest_ready ? 4.0f : 0.0f; // latest complete shown - no tears
        default:                                            return 0.0f;
    }
}

auto Surface_impl::get_surface_format() -> VkSurfaceFormatKHR const
{
    return m_surface_format;
}

auto Surface_impl::get_present_mode() -> VkPresentModeKHR const
{
    return m_present_mode;
}

auto Surface_impl::get_image_count() -> uint32_t const
{
    return m_image_count;
}

auto Surface_impl::get_vulkan_surface() -> VkSurfaceKHR const
{
    return m_surface;
}

void Surface_impl::choose_surface_format()
{
    float best_score = std::numeric_limits<float>::lowest();
    VkSurfaceFormatKHR selected_format{
        .format     = VK_FORMAT_UNDEFINED,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    };
    log_context->debug("Surface formats:");
    for (const VkSurfaceFormatKHR& surface_format : m_surface_formats) {
        const float score = get_surface_format_score(surface_format);
        log_context->debug("  format {} colorspace {} score {}", c_str(surface_format.format), c_str(surface_format.colorSpace), score);

        if (score > best_score) {
            best_score      = score;
            selected_format = surface_format;
        }
    }
    m_surface_format = selected_format;
    log_context->debug("Selected surface format {} color space {}", c_str(m_surface_format.format), c_str(m_surface_format.colorSpace));
}

void Surface_impl::choose_present_mode()
{
    float best_score = std::numeric_limits<float>::lowest();
    VkPresentModeKHR selected_present_mode = VK_PRESENT_MODE_FIFO_KHR;
    log_context->debug("Surface present modes:");
    for (const VkPresentModeKHR present_mode : m_present_modes) {
        const float score = get_present_mode_score(present_mode);
        log_context->debug("  present mode {} score {}", c_str(present_mode), score);
        if (score > best_score) {
            best_score            = score;
            selected_present_mode = present_mode;
        }
    }
    m_present_mode = selected_present_mode;
    log_context->debug("Selected present mode {}", c_str(m_present_mode));
}

Surface_impl::~Surface_impl() noexcept
{
    log_context->debug("Surface_impl::~Surface_impl()");
    VkInstance instance = m_device_impl.get_vulkan_instance();
    VkSurfaceKHR surface = m_surface;
    if (surface != VK_NULL_HANDLE) {
        m_device_impl.add_completion_handler(
            [instance, surface]() {
                log_context->debug("Surface_impl::~Surface_impl() completion handler");
                vkDestroySurfaceKHR(instance, surface, nullptr);
            }
        );
    }
}

auto Surface_impl::update_swapchain(VkExtent2D& extent_out) -> VkSwapchainKHR const
{
    log_context->debug("Surface_impl::update_swapchain()");

    VkSurfaceCapabilitiesKHR surface_capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &surface_capabilities);
    if (result != VK_SUCCESS) {
        log_debug->warn("vkGetPhysicalDeviceSurfaceCapabilitiesKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        return VK_NULL_HANDLE;
    }

    if (
        (surface_capabilities.currentExtent.width  == std::numeric_limits<uint32_t>::max()) ||
        (surface_capabilities.currentExtent.height == std::numeric_limits<uint32_t>::max())
    ) {
        extent_out.width  = m_surface_create_info.context_window->get_width();
        extent_out.height = m_surface_create_info.context_window->get_height();
        log_debug->debug("  Surface current extent not set, using window size {} x {}", extent_out.width, extent_out.height);
    } else {
        extent_out.width  = surface_capabilities.currentExtent.width;
        extent_out.height = surface_capabilities.currentExtent.height;
        log_debug->debug("  Surface current extent : {} x {}", extent_out.width, extent_out.height);
    }
    log_debug->debug("  Surface min extent : {} x {}", surface_capabilities.minImageExtent.width, surface_capabilities.minImageExtent.height);
    log_debug->debug("  Surface max extent : {} x {}", surface_capabilities.maxImageExtent.width, surface_capabilities.maxImageExtent.height);

    // Clamp swapchain size
    extent_out.width  = extent_out.width;
    extent_out.height = extent_out.height;
    extent_out.width  = std::min(extent_out.width,  surface_capabilities.maxImageExtent.width);
    extent_out.height = std::min(extent_out.height, surface_capabilities.maxImageExtent.height);
    extent_out.width  = std::max(extent_out.width,  surface_capabilities.minImageExtent.width);
    extent_out.height = std::max(extent_out.height, surface_capabilities.minImageExtent.height);

    // TODO handle cases where currentExtent is not the right thing
    if (
        (m_current_swapchain_extent.width  == extent_out.width) &&
        (m_current_swapchain_extent.height == extent_out.height)
    ) {
        extent_out = m_current_swapchain_extent;
        return m_current_swapchain;
    }

    if (
        (surface_capabilities.currentExtent.width  == 0) ||
        (surface_capabilities.currentExtent.height == 0)
    ){
        extent_out = m_current_swapchain_extent;
        return VK_NULL_HANDLE;
    }
    VkDevice       vulkan_device               = m_device_impl.get_vulkan_device();
    uint32_t       graphics_queue_family_index = m_device_impl.get_graphics_queue_family_index();
    VkSwapchainKHR old_swapchain               = m_current_swapchain;
    log_debug->debug(
        "Calling vkCreateSwapchainKHR(format = {}, colorSpace = {}, extent = {} x {}, presentMode {}, oldSwapchain = {})",
        c_str(m_surface_format.format),
        c_str(m_surface_format.colorSpace),
        extent_out.width,
        extent_out.height,
        c_str(m_present_mode),
        fmt::ptr(old_swapchain)
    );
    const VkSwapchainCreateInfoKHR swapchain_create_info{
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,// VkStructureType
        .pNext                 = nullptr,                               // const void*
        .flags                 = 0,                                     // VkSwapchainCreateFlagsKHR
        .surface               = m_surface,                             // VkSurfaceKHR
        .minImageCount         = m_image_count,                         // uint32_t
        .imageFormat           = m_surface_format.format,               // VkFormat
        .imageColorSpace       = m_surface_format.colorSpace,           // VkColorSpaceKHR
        .imageExtent           = extent_out,                            // VkExtent2D
        .imageArrayLayers      = 1,                                     // uint32_t
        .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,   // VkImageUsageFlags
        .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,             // VkSharingMode
        .queueFamilyIndexCount = 1,                                     // uint32_t
        .pQueueFamilyIndices   = &graphics_queue_family_index,          // const uint32_t*
        .preTransform          = surface_capabilities.currentTransform, // VkSurfaceTransformFlagBitsKHR
        .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,     // VkCompositeAlphaFlagBitsKHR
        .presentMode           = m_present_mode,                        // VkPresentModeKHR
        .clipped               = VK_TRUE,                               // VkBool32
        .oldSwapchain          = old_swapchain                          // VkSwapchainKHR
    };

    VkSwapchainKHR vulkan_swapchain{VK_NULL_HANDLE};
    result = vkCreateSwapchainKHR(vulkan_device, &swapchain_create_info, nullptr, &vulkan_swapchain);
    if (result != VK_SUCCESS) {
        log_debug->warn("vkCreateSwapchainKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
    }

    if ((result != VK_SUCCESS) && (vulkan_swapchain != VK_NULL_HANDLE)) {
        vkDestroySwapchainKHR(vulkan_device, vulkan_swapchain, nullptr);
        return VK_NULL_HANDLE;
    }

    if (old_swapchain != VK_NULL_HANDLE) {
        m_device_impl.add_completion_handler(
            [vulkan_device, old_swapchain]() {
                vkDestroySwapchainKHR(vulkan_device, old_swapchain, nullptr);
            }
        );
    }

    m_current_swapchain        = vulkan_swapchain;
    m_current_swapchain_extent = swapchain_create_info.imageExtent;
    return vulkan_swapchain;
}

} // namespace erhe::graphics
