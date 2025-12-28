// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
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

    m_swapchain = std::make_unique<Swapchain>(
        std::make_unique<Swapchain_impl>(m_device_impl, *this)
    );
}

auto Surface_impl::get_swapchain() -> Swapchain*
{
    return m_swapchain.get();
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

    const Capabilities& capabilities = m_device_impl.get_capabilities();
    const bool use_surface_capabilities2 = capabilities.m_surface_capabilities2;
    //const bool use_surface_maintenance1  = capabilities.m_surface_maintenance1;
    VkResult result = VK_SUCCESS;

    VkSurfaceCapabilities2KHR surface_capabilities2{
        .sType               = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
        .pNext               = nullptr,
        .surfaceCapabilities = {}
    };

    if (use_surface_capabilities2) {

        // VkBaseOutStructure* chain_last = reinterpret_cast<VkBaseOutStructure*>(&m_surface_capabilities2);

        //m_scaling_capabilities = VkSurfacePresentScalingCapabilitiesKHR{
        //    .sType                    = VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_KHR, // VkStructureType
        //    .pNext                    = nullptr,                                                    // void*
        //    .supportedPresentScaling  = VK_PRESENT_GRAVITY_FLAG_BITS_MAX_ENUM_KHR,                  // VkPresentScalingFlagsKHR
        //    .supportedPresentGravityX = VK_PRESENT_GRAVITY_FLAG_BITS_MAX_ENUM_KHR,                  // VkPresentGravityFlagsKHR
        //    .supportedPresentGravityY = VK_PRESENT_GRAVITY_FLAG_BITS_MAX_ENUM_KHR,                  // VkPresentGravityFlagsKHR
        //    .minScaledImageExtent     = {1,1},                                                      // VkExtent2D
        //    .maxScaledImageExtent     = {1,1}                                                       // VkExtent2D
        //};
        //if (use_surface_maintenance1) {
        //    chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&m_scaling_capabilities);
        //    chain_last        = chain_last->pNext;
        //}

        // const VkSurfacePresentModeKHR surface_present_mode{
        //     .sType       = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR, // VkStructureType 
        //     .pNext       = nullptr, // void*           
        //     .presentMode = present_mode, // VkPresentModeKHR
        // };
        // 
        const VkPhysicalDeviceSurfaceInfo2KHR surface_info{
            .sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
            .pNext   = nullptr,
            .surface = m_surface
        };

        result = vkGetPhysicalDeviceSurfaceCapabilities2KHR(physical_device, &surface_info, &surface_capabilities2);
        if (result != VK_SUCCESS) {
            fail();
            return false;
        }

        // log_context->debug("Supported present scaling   = {}", to_string(m_scaling_capabilities.supportedPresentScaling));
        // log_context->debug("Supported present gravity X = {}", to_string(m_scaling_capabilities.supportedPresentGravityX));
        // log_context->debug("Supported present gravity Y = {}", to_string(m_scaling_capabilities.supportedPresentGravityY));

    } else {

        result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, m_surface, &surface_capabilities2.surfaceCapabilities);
        if (result != VK_SUCCESS) {
            fail();
            return false;
        }
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

    m_physical_device = physical_device;

    choose_present_mode();

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
    m_image_count = std::max(m_image_count, surface_capabilities2.surfaceCapabilities.minImageCount);
    if (surface_capabilities2.surfaceCapabilities.maxImageCount != 0) {
        m_image_count = std::min(
            m_image_count,
            surface_capabilities2.surfaceCapabilities.maxImageCount
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

auto Surface_impl::get_present_scaling_capabilities() const -> VkSurfacePresentScalingCapabilitiesKHR
{
    const Capabilities& capabilities = m_device_impl.get_capabilities();
    const bool use_surface_capabilities2 = capabilities.m_surface_capabilities2;
    const bool use_surface_maintenance1  = capabilities.m_surface_maintenance1;
    VkResult result = VK_SUCCESS;

    VkSurfacePresentScalingCapabilitiesKHR scaling_capabilities{
        .sType                    = VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_KHR,
        .pNext                    = nullptr,
        .supportedPresentScaling  = 0,
        .supportedPresentGravityX = 0,
        .supportedPresentGravityY = 0,
        .minScaledImageExtent     = {0, 0},
        .maxScaledImageExtent     = {0, 0}
    };
    if (use_surface_capabilities2 && use_surface_maintenance1) {
        VkSurfaceCapabilities2KHR surface_capabilities2{
            .sType               = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
            .pNext               = &scaling_capabilities,
            .surfaceCapabilities = {}
        };
        VkSurfacePresentModeKHR surface_present_mode{
            .sType       = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR,
            .pNext       = nullptr,
            .presentMode = m_present_mode
        };
        const VkPhysicalDeviceSurfaceInfo2KHR surface_info{
            .sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
            .pNext   = &surface_present_mode,
            .surface = m_surface
        };
        result = vkGetPhysicalDeviceSurfaceCapabilities2KHR(m_physical_device, &surface_info, &surface_capabilities2);
        //if (result == VK_SUCCESS) {
        //    return scaling_capabilities;
        //}
    }
    return scaling_capabilities;
}

auto Surface_impl::get_present_mode_score(const VkPresentModeKHR present_mode) const -> float
{
    // https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#VkSurfacePresentModeKHR
    // https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_surface_maintenance1.html

    const Capabilities& capabilities = m_device_impl.get_capabilities();
    const bool use_surface_capabilities2 = capabilities.m_surface_capabilities2;
    const bool use_surface_maintenance1  = capabilities.m_surface_maintenance1;
    VkResult result = VK_SUCCESS;

    float scaling_score = 1.0f;
    VkSurfacePresentScalingCapabilitiesKHR scaling_capabilities{
        .sType                    = VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_KHR,
        .pNext                    = nullptr,
        .supportedPresentScaling  = 0,
        .supportedPresentGravityX = 0,
        .supportedPresentGravityY = 0,
        .minScaledImageExtent     = {0, 0},
        .maxScaledImageExtent     = {0, 0}
    };
    log_context->debug("present mode {}", c_str(present_mode));
    if (use_surface_capabilities2 && use_surface_maintenance1) {
        VkSurfaceCapabilities2KHR surface_capabilities2{
            .sType               = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
            .pNext               = &scaling_capabilities,
            .surfaceCapabilities = {}
        };
        VkSurfacePresentModeKHR surface_present_mode{
            .sType       = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR,
            .pNext       = nullptr,
            .presentMode = present_mode
        };
        const VkPhysicalDeviceSurfaceInfo2KHR surface_info{
            .sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
            .pNext   = &surface_present_mode,
            .surface = m_surface
        };
        result = vkGetPhysicalDeviceSurfaceCapabilities2KHR(m_physical_device, &surface_info, &surface_capabilities2);
        if (result == VK_SUCCESS) {
            log_context->debug("  scaling   = {}", to_string_VkPresentScalingFlagsKHR(scaling_capabilities.supportedPresentScaling));
            log_context->debug("  gravity X = {}", to_string_VkPresentGravityFlagsKHR(scaling_capabilities.supportedPresentGravityX));
            log_context->debug("  gravity Y = {}", to_string_VkPresentGravityFlagsKHR(scaling_capabilities.supportedPresentGravityY));
            const bool one_to_one           = (scaling_capabilities.supportedPresentScaling & VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR          ) != 0;
            const bool aspect_ratio_stretch = (scaling_capabilities.supportedPresentScaling & VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR) != 0;
            const bool stretch              = (scaling_capabilities.supportedPresentScaling & VK_PRESENT_SCALING_STRETCH_BIT_KHR             ) != 0;

            const bool x_min                = (scaling_capabilities.supportedPresentGravityX & VK_PRESENT_GRAVITY_MIN_BIT_KHR     ) != 0;
            const bool x_max                = (scaling_capabilities.supportedPresentGravityX & VK_PRESENT_GRAVITY_MAX_BIT_KHR     ) != 0;
            const bool x_centered           = (scaling_capabilities.supportedPresentGravityX & VK_PRESENT_GRAVITY_CENTERED_BIT_KHR) != 0;

            const bool y_min                = (scaling_capabilities.supportedPresentGravityY & VK_PRESENT_GRAVITY_MIN_BIT_KHR     ) != 0;
            const bool y_max                = (scaling_capabilities.supportedPresentGravityY & VK_PRESENT_GRAVITY_MAX_BIT_KHR     ) != 0;
            const bool y_centered           = (scaling_capabilities.supportedPresentGravityY & VK_PRESENT_GRAVITY_CENTERED_BIT_KHR) != 0;

            // TODO Better scoring scheme?
            if (one_to_one          ) scaling_score += 1.0f;
            if (stretch             ) scaling_score += 1.0f;
            if (aspect_ratio_stretch) scaling_score += 1.0f;
            if (x_min               ) scaling_score += 1.0f;
            if (x_max               ) scaling_score += 1.0f;
            if (x_centered          ) scaling_score += 1.0f;
            if (y_min               ) scaling_score += 1.0f;
            if (y_max               ) scaling_score += 1.0f;
            if (y_centered          ) scaling_score += 1.0f;
        }
    }

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
    //   engine must update the current image with  out any need for further presentation requests.
    //   The application can indicate the image contents have been updated by making a presentation request, but
    //   this does not guarantee the timing of when it will be updated.
    //   This mode may result in visible tearing if rendering to the image is not timed correctly.

    const bool latest_ready = m_device_impl.get_capabilities().m_present_mode_fifo_latest_ready;

    // TODO How to combine scaling score?
    switch (present_mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:                 return 1.0f * 10.0f + scaling_score; // fastest - tears
        case VK_PRESENT_MODE_MAILBOX_KHR:                   return 4.0f * 10.0f + scaling_score; // latest complete shown - no tears
        case VK_PRESENT_MODE_FIFO_KHR:                      return 2.0f * 10.0f + scaling_score; // classic vsync - no tears
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:              return 3.0f * 10.0f + scaling_score; // late frames presented immediately - some tears
        case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:     return 0.0f;
        case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: return 0.0f;
        case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR:         return latest_ready ? 5.0f * 10.0f + scaling_score : 0.0f; // latest complete shown - no tears
        default:
            return 0.0f;
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

    const VkSurfacePresentScalingCapabilitiesKHR scaling_capabilities = get_present_scaling_capabilities();
    log_context->debug("  scaling   = {}", to_string_VkPresentScalingFlagsKHR(scaling_capabilities.supportedPresentScaling));
    log_context->debug("  gravity X = {}", to_string_VkPresentGravityFlagsKHR(scaling_capabilities.supportedPresentGravityX));
    log_context->debug("  gravity Y = {}", to_string_VkPresentGravityFlagsKHR(scaling_capabilities.supportedPresentGravityY));
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

auto Surface_impl::is_empty() -> bool
{
    return m_is_empty;
}

auto Surface_impl::is_valid() -> bool
{
    return m_is_valid;
}

auto Surface_impl::update_swapchain(Vulkan_swapchain_create_info& out_swapchain_create_info) -> bool
{
    log_context->debug("Surface_impl::configure_swapchain()");

    VkSurfaceCapabilitiesKHR surface_capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &surface_capabilities);
    m_is_valid = result == VK_SUCCESS;
    if (result != VK_SUCCESS) {
        log_context->warn("vkGetPhysicalDeviceSurfaceCapabilitiesKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        return false;
    }

    VkExtent2D extent{.width = 0, .height = 0};
    if (
        (surface_capabilities.currentExtent.width  == std::numeric_limits<uint32_t>::max()) ||
        (surface_capabilities.currentExtent.height == std::numeric_limits<uint32_t>::max())
    ) {
        extent.width  = m_surface_create_info.context_window->get_width();
        extent.height = m_surface_create_info.context_window->get_height();
        log_context->debug("  Surface current extent not set, using window size {} x {}", extent.width, extent.height);
    } else {
        extent.width  = surface_capabilities.currentExtent.width;
        extent.height = surface_capabilities.currentExtent.height;
        log_context->debug("  Surface current extent : {} x {}", extent.width, extent.height);
    }
    log_context->debug("  Surface min extent : {} x {}", surface_capabilities.minImageExtent.width, surface_capabilities.minImageExtent.height);
    log_context->debug("  Surface max extent : {} x {}", surface_capabilities.maxImageExtent.width, surface_capabilities.maxImageExtent.height);

    // Clamp swapchain size
    extent.width  = extent.width;
    extent.height = extent.height;
    extent.width  = std::min(extent.width,  surface_capabilities.maxImageExtent.width);
    extent.height = std::min(extent.height, surface_capabilities.maxImageExtent.height);                                                                                                    
    extent.width  = std::max(extent.width,  surface_capabilities.minImageExtent.width);
    extent.height = std::max(extent.height, surface_capabilities.minImageExtent.height);

    m_is_empty = 
        (surface_capabilities.currentExtent.width  == 0) ||
        (surface_capabilities.currentExtent.height == 0);

    if (m_is_empty) {
        extent = VkExtent2D{.width = 0, .height = 0};
        return false;
    }

    if (
        (extent.width  == m_swapchain_extent.width) &&
        (extent.height == m_swapchain_extent.height)
    ) {
        return false;
    }

    uint32_t       graphics_queue_family_index = m_device_impl.get_graphics_queue_family_index();
    VkSwapchainKHR old_swapchain               = m_vulkan_swapchain;
    bool           use_scaling                 = m_device_impl.get_capabilities().m_surface_maintenance1;

    log_context->debug(
        "Calling vkCreateSwapchainKHR(format = {}, colorSpace = {}, extent = {} x {}, presentMode {}, oldSwapchain = {})",
        c_str(m_surface_format.format),
        c_str(m_surface_format.colorSpace),
        extent.width,
        extent.height,
        c_str(m_present_mode),
        fmt::ptr(old_swapchain)
    );

    // Scaling behavior options:
    //  - VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR
    //  - VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR
    //  - VK_PRESENT_SCALING_STRETCH_BIT_KHR
    // Present gravity options:
    //  - VK_PRESENT_GRAVITY_MIN_BIT_KHR
    //  - VK_PRESENT_GRAVITY_MAX_BIT_KHR
    //  - VK_PRESENT_GRAVITY_CENTERED_BIT_KHR
    VkPresentScalingFlagsEXT scaling_behavior = 0;
    VkPresentGravityFlagsEXT gravity_x        = 0;
    VkPresentGravityFlagsEXT gravity_y        = 0;
    if (use_scaling) {
        const VkSurfacePresentScalingCapabilitiesKHR scaling_capabilities = get_present_scaling_capabilities();

        if (
            ((scaling_capabilities.minScaledImageExtent.width  != 0xffffffffu) && (extent.width  < scaling_capabilities.minScaledImageExtent.width )) ||
            ((scaling_capabilities.minScaledImageExtent.height != 0xffffffffu) && (extent.height < scaling_capabilities.minScaledImageExtent.height)) ||
            ((scaling_capabilities.maxScaledImageExtent.width  != 0xffffffffu) && (extent.width  > scaling_capabilities.maxScaledImageExtent.width )) ||
            ((scaling_capabilities.maxScaledImageExtent.height != 0xffffffffu) && (extent.height > scaling_capabilities.maxScaledImageExtent.height))
        ) {
            log_context->debug(
                "  swapchain extent {} x {} outside min {} x {}, max {} x {}",
                extent.width, extent.height,
                scaling_capabilities.minScaledImageExtent.width, scaling_capabilities.minScaledImageExtent.height,
                scaling_capabilities.maxScaledImageExtent.width, scaling_capabilities.maxScaledImageExtent.height
            );
            use_scaling = false;
        } else {
            const bool one_to_one           = (scaling_capabilities.supportedPresentScaling & VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR          ) != 0;
            const bool aspect_ratio_stretch = (scaling_capabilities.supportedPresentScaling & VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR) != 0;
            const bool stretch              = (scaling_capabilities.supportedPresentScaling & VK_PRESENT_SCALING_STRETCH_BIT_KHR             ) != 0;

            const bool x_min                = (scaling_capabilities.supportedPresentGravityX & VK_PRESENT_GRAVITY_MIN_BIT_KHR     ) != 0;
            const bool x_max                = (scaling_capabilities.supportedPresentGravityX & VK_PRESENT_GRAVITY_MAX_BIT_KHR     ) != 0;
            const bool x_centered           = (scaling_capabilities.supportedPresentGravityX & VK_PRESENT_GRAVITY_CENTERED_BIT_KHR) != 0;

            const bool y_min                = (scaling_capabilities.supportedPresentGravityY & VK_PRESENT_GRAVITY_MIN_BIT_KHR     ) != 0;
            const bool y_max                = (scaling_capabilities.supportedPresentGravityY & VK_PRESENT_GRAVITY_MAX_BIT_KHR     ) != 0;
            const bool y_centered           = (scaling_capabilities.supportedPresentGravityY & VK_PRESENT_GRAVITY_CENTERED_BIT_KHR) != 0;

            // TODO VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR does not work on AMD / Windows
            if (aspect_ratio_stretch) {
                scaling_behavior = VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR;
            } else if (stretch) {
                scaling_behavior = VK_PRESENT_SCALING_STRETCH_BIT_KHR;
            } else if (one_to_one) {
                scaling_behavior = VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR;
            }
            //if (stretch) {
            //    scaling_behavior = VK_PRESENT_SCALING_STRETCH_BIT_KHR;
            //} else if (aspect_ratio_stretch) {
            //    scaling_behavior = VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR;
            //} else if (one_to_one) {
            //    scaling_behavior = VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR;
            //}

            if (x_centered) {
                gravity_x = VK_PRESENT_GRAVITY_CENTERED_BIT_KHR;
            } else if (x_min) {
                gravity_x = VK_PRESENT_GRAVITY_MIN_BIT_KHR;
            } else if (x_max) {
                gravity_x = VK_PRESENT_GRAVITY_MAX_BIT_KHR;
            }

            if (y_centered) {
                gravity_y = VK_PRESENT_GRAVITY_CENTERED_BIT_KHR;
            } else if (y_min) {
                gravity_y = VK_PRESENT_GRAVITY_MIN_BIT_KHR;
            } else if (y_max) {
                gravity_y = VK_PRESENT_GRAVITY_MAX_BIT_KHR;
            }
            log_context->debug("  scaling behavior = {}", to_string_VkPresentScalingFlagsKHR(scaling_behavior));
            log_context->debug("  gravity x        = {}", to_string_VkPresentGravityFlagsKHR(gravity_x));
            log_context->debug("  gravity y        = {}", to_string_VkPresentGravityFlagsKHR(gravity_y));
        }
    }

    out_swapchain_create_info.swapchain_present_scaling_create_info = VkSwapchainPresentScalingCreateInfoKHR{
        .sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_KHR, // VkStructureType
        .pNext           = nullptr,                                                     // const void*
        .scalingBehavior = scaling_behavior,                                            // VkPresentScalingFlagsKHR
        .presentGravityX = gravity_x,                                                   // VkPresentGravityFlagsKHR
        .presentGravityY = gravity_y                                                    // VkPresentGravityFlagsKHR
    };

    out_swapchain_create_info.swapchain_create_info = VkSwapchainCreateInfoKHR{
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,   // VkStructureType
        .pNext                 = use_scaling                                    // const void*
                                    ? &out_swapchain_create_info.swapchain_present_scaling_create_info
                                    : nullptr,        
        .flags                 = 0,                                             // VkSwapchainCreateFlagsKHR
        .surface               = m_surface,                                     // VkSurfaceKHR
        .minImageCount         = m_image_count,                                 // uint32_t
        .imageFormat           = m_surface_format.format,                       // VkFormat
        .imageColorSpace       = m_surface_format.colorSpace,                   // VkColorSpaceKHR
        .imageExtent           = extent,                                        // VkExtent2D
        .imageArrayLayers      = 1,                                             // uint32_t
        .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,           // VkImageUsageFlags
        .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,                     // VkSharingMode
        .queueFamilyIndexCount = 1,                                             // uint32_t
        .pQueueFamilyIndices   = &graphics_queue_family_index,                  // const uint32_t*
        .preTransform          = surface_capabilities.currentTransform,         // VkSurfaceTransformFlagBitsKHR
        .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,             // VkCompositeAlphaFlagBitsKHR
        .presentMode           = m_present_mode,                                // VkPresentModeKHR
        .clipped               = VK_TRUE,                                       // VkBool32
        .oldSwapchain          = nullptr //old_swapchain                                  // VkSwapchainKHR
    };
    return true;
}

} // namespace erhe::graphics

//VkSurfacePresentModeCompatibilityKHR present_mode_compatibility{
//    .sType            = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_KHR, // VkStructureType
//    .pNext            = nullptr,
//    .presentModeCount = 0,
//    .pPresentModes    = nullptr
//};
