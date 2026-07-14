// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_emulated_swapchain.hpp"
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

Surface_impl::Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info, const bool headless)
    : m_device_impl        {device_impl}
    , m_surface_create_info{create_info}
    , m_surface_format{
        .format     = VK_FORMAT_UNDEFINED,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    }
    , m_present_mode      {VK_PRESENT_MODE_FIFO_KHR}
    , m_headless          {headless}
{
    ERHE_VERIFY(create_info.context_window != nullptr);

    if (m_headless) {
        // Surfaceless: no VkSurfaceKHR. A dedicated Emulated_swapchain_impl
        // provides offscreen images in place of a real swapchain; the real
        // WSI Swapchain_impl is not constructed.
        m_emulated_swapchain = std::make_unique<Emulated_swapchain_impl>(m_device_impl, *this);
        return;
    }

    const VkInstance vulkan_instance = device_impl.get_vulkan_instance();
    void* surface = create_info.context_window->create_vulkan_surface(static_cast<void*>(vulkan_instance));
    m_surface = static_cast<VkSurfaceKHR>(surface);

    m_swapchain = std::make_unique<Swapchain>(
        std::make_unique<Swapchain_impl>(m_device_impl, *this)
    );
}

auto Surface_impl::get_surface_create_info() const -> const Surface_create_info&
{
    return m_surface_create_info;
}

auto Surface_impl::get_swapchain() -> Swapchain*
{
    return m_swapchain.get();
}

auto Surface_impl::is_headless() const -> bool
{
    return m_headless;
}

auto Surface_impl::get_emulated_swapchain() const -> Emulated_swapchain_impl*
{
    return m_emulated_swapchain.get();
}

auto Surface_impl::get_color_format() -> erhe::dataformat::Format
{
    if (m_headless) {
        return m_emulated_swapchain->get_color_format();
    }
    if (m_swapchain != nullptr) {
        return m_swapchain->get_color_format();
    }
    return erhe::dataformat::Format::format_undefined;
}

auto Surface_impl::get_depth_format() -> erhe::dataformat::Format
{
    if (m_headless) {
        return m_emulated_swapchain->get_depth_format();
    }
    if (m_swapchain != nullptr) {
        return m_swapchain->get_depth_format();
    }
    return erhe::dataformat::Format::format_undefined;
}

auto Surface_impl::can_use_physical_device(VkPhysicalDevice physical_device) -> bool
{
    if (m_headless) {
        // Surfaceless: any graphics-capable device is acceptable; there are no
        // surface formats / present modes to query.
        static_cast<void>(physical_device);
        return true;
    }
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
    if (m_headless) {
        m_physical_device = physical_device;
        // Pick an _SRGB color format so the emulated swapchain image carries
        // the sRGB OETF erhe's linear shader output relies on. Prefer
        // B8G8R8A8_SRGB, fall back to R8G8B8A8_SRGB.
        m_surface_format = VkSurfaceFormatKHR{
            .format     = VK_FORMAT_R8G8B8A8_SRGB,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        };
        for (const VkFormat candidate : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB}) {
            VkFormatProperties format_properties{};
            vkGetPhysicalDeviceFormatProperties(physical_device, candidate, &format_properties);
            if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) {
                m_surface_format.format = candidate;
                break;
            }
        }
        m_present_mode = VK_PRESENT_MODE_FIFO_KHR;
        m_image_count  = 3;
        log_context->info(
            "Headless surface: emulated swapchain format {} image count {}",
            c_str(m_surface_format.format), m_image_count
        );
        return true;
    }
    if (m_surface == VK_NULL_HANDLE) {
        fail();
        return false;
    }

    const Capabilities& capabilities = m_device_impl.get_capabilities();
    const bool use_surface_capabilities2 = capabilities.m_surface_capabilities2;
    VkResult result = VK_SUCCESS;

    VkSurfaceCapabilities2KHR surface_capabilities2{
        .sType               = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
        .pNext               = nullptr,
        .surfaceCapabilities = {}
    };

    if (use_surface_capabilities2) {
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
    log_context->info(
        "Swapchain image count = {} (driver min = {}, max = {})",
        m_image_count,
        surface_capabilities2.surfaceCapabilities.minImageCount,
        surface_capabilities2.surfaceCapabilities.maxImageCount
    );

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
    const int requested_bit_depth = (m_surface_create_info.context_window != nullptr)
        ? m_surface_create_info.context_window->get_window_configuration().color_bit_depth
        : 8;
    const bool prefer_hdr = m_surface_create_info.prefer_high_dynamic_range;

    // Classify the format
    bool is_srgb_format  = false; // format applies linear-to-sRGB OETF on write
    bool is_float_format = false;
    int  bit_depth       = 0;
    switch (surface_format.format) {
        case VK_FORMAT_R8G8B8A8_SRGB:            is_srgb_format = true;  bit_depth = 8;  break;
        case VK_FORMAT_B8G8R8A8_SRGB:            is_srgb_format = true;  bit_depth = 8;  break;
        case VK_FORMAT_R8G8B8A8_UNORM:           is_srgb_format = false; bit_depth = 8;  break;
        case VK_FORMAT_B8G8R8A8_UNORM:           is_srgb_format = false; bit_depth = 8;  break;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: is_srgb_format = false; bit_depth = 10; break;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: is_srgb_format = false; bit_depth = 10; break;
        case VK_FORMAT_R16G16B16A16_SFLOAT:      is_float_format = true; bit_depth = 16; break;
        default:
            return 0.0f;
    }

    // Score format + colorSpace as a pair.
    //
    // erhe shaders output linear color. The format+colorSpace combination must
    // ensure correct display:
    //
    //   _SRGB format + SRGB_NONLINEAR:        HW encodes sRGB, display expects sRGB.  Correct.
    //   _UNORM format + SRGB_NONLINEAR:        Linear stored as-is, display expects sRGB. Dark.
    //   _SFLOAT format + EXTENDED_SRGB_LINEAR:  Linear stored, compositor converts. Correct (HDR).
    //   _UNORM 10-bit + HDR10_ST2084:           Needs PQ encoding in shader. Not supported.
    //
    // Reject mismatched pairs that would produce incorrect output.

    const VkColorSpaceKHR color_space = surface_format.colorSpace;

    if (color_space == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        // Standard SDR path. Only correct when the format has sRGB OETF.
        if (!is_srgb_format) {
            // UNORM + SRGB_NONLINEAR = linear values displayed as sRGB = dark.
            // Give a low but non-zero score as a last-resort fallback.
            return 1.0f;
        }
        // sRGB format + SRGB_NONLINEAR: the correct SDR combination.
        float score = 100.0f;
        if (bit_depth == requested_bit_depth) {
            score += 40.0f;
        }
        if (!prefer_hdr) {
            score += 20.0f; // SDR preference bonus
        }
        return score;
    }

    if (color_space == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
        // scRGB linear HDR path. Only makes sense with float formats.
        if (!is_float_format) {
            return 0.0f;
        }
        float score = 80.0f;
        if (prefer_hdr) {
            score += 40.0f; // HDR preference bonus
        }
        return score;
    }

    // Other color spaces (HDR10_ST2084, Display P3, BT2020, etc.)
    // require shader-side transfer functions erhe does not currently implement.
    // Score low so they are only selected if nothing else is available.
    return 0.5f;
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

auto Surface_impl::get_surface_format() const -> VkSurfaceFormatKHR
{
    return m_surface_format;
}

auto Surface_impl::get_present_mode() const -> VkPresentModeKHR
{
    return m_present_mode;
}

auto Surface_impl::get_image_count() const -> uint32_t
{
    return m_image_count;
}

auto Surface_impl::get_vulkan_surface() const -> VkSurfaceKHR
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
    const Graphics_config& config = m_device_impl.get_graphics_config();

    // Always report what the driver advertises and what we pick -- frame-
    // pacing issues (and especially the MoltenVK FIFO-at-60Hz trap) are
    // impossible to diagnose without this.
    {
        std::string modes;
        for (const VkPresentModeKHR present_mode : m_present_modes) {
            if (!modes.empty()) {
                modes += ", ";
            }
            modes += c_str(present_mode);
        }
        log_context->info("Surface present modes available: [{}]", modes);
    }

    // When force_disable_vsync is set, use IMMEDIATE if available.
    // This disables vsync for accurate performance measurements.
    if (config.force_disable_vsync) {
        for (const VkPresentModeKHR present_mode : m_present_modes) {
            if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                m_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                log_context->info("Forcing present mode VK_PRESENT_MODE_IMMEDIATE_KHR (force_disable_vsync)");
                return;
            }
        }
        log_context->warn("force_disable_vsync is set but VK_PRESENT_MODE_IMMEDIATE_KHR is not available");
    }

    float best_score = std::numeric_limits<float>::lowest();
    VkPresentModeKHR selected_present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (const VkPresentModeKHR present_mode : m_present_modes) {
        const float score = get_present_mode_score(present_mode);
        log_context->debug("  present mode {} score {}", c_str(present_mode), score);
        if (score > best_score) {
            best_score            = score;
            selected_present_mode = present_mode;
        }
    }
    m_present_mode = selected_present_mode;
    log_context->info("Selected present mode {}", c_str(m_present_mode));

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
            [instance, surface](Device_impl&) {
                log_context->debug("Surface_impl::~Surface_impl() completion handler");
                vkDestroySurfaceKHR(instance, surface, nullptr);
            }
        );
    }
}

auto Surface_impl::recreate_for_new_window() -> bool
{
    log_context->info("Surface_impl::recreate_for_new_window()");

    if (m_headless) {
        log_context->error("Surface_impl::recreate_for_new_window() called on a headless surface");
        return false;
    }

    if (m_surface_create_info.context_window == nullptr) {
        log_context->error("Surface_impl::recreate_for_new_window() with no Context_window");
        return false;
    }

    const VkInstance       instance         = m_device_impl.get_vulkan_instance();
    const VkPhysicalDevice cached_phys_dev  = m_physical_device;

    // 1. Make sure the GPU is done touching the old swapchain images so it
    //    is safe to destroy them. Block here, not via completion handler:
    //    we are about to throw away the swapchain handle the device-frame
    //    submit history references.
    m_device_impl.wait_idle();

    // 2. Tear down the swapchain's Vulkan-side state but keep the
    //    Swapchain (and Swapchain_impl) C++ object alive. Render_pass
    //    objects created against this swapchain (most importantly
    //    Window_imgui_host::m_render_pass and any cached swapchain-
    //    targeted Render_pass elsewhere) hold raw Swapchain* pointers
    //    inside Render_pass_impl::m_swapchain that they dereference
    //    every frame to acquire a framebuffer; destroying the Swapchain
    //    object underneath them leaves those pointers dangling and the
    //    next render pass crashes inside the driver. reset_for_new_surface
    //    recycles per-slot fences/semaphores, drains the present
    //    history, frees old swapchain garbage, and finally calls
    //    vkDestroySwapchainKHR -- per Vulkan spec, all VkSwapchainKHRs
    //    derived from a VkSurfaceKHR must be destroyed before
    //    vkDestroySurfaceKHR.
    if (m_swapchain != nullptr) {
        m_swapchain->get_impl().reset_for_new_surface();
    }

    // 3. Destroy the old VkSurfaceKHR. The native window it wraps has
    //    been detached by the OS already; this just releases the Vulkan
    //    handle synchronously (no completion-handler defer -- we want
    //    the new VkSurfaceKHR slot available before SDL hands us one).
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    // Reset cached state so the next use_physical_device pass refills
    // it from the new VkSurfaceKHR. m_swapchain_extent must be cleared
    // too: update_swapchain compares the queried extent against this
    // cached value and skips swapchain creation when they match. After
    // reset_for_new_surface the Swapchain_impl has m_vulkan_swapchain
    // == VK_NULL_HANDLE; without clearing the extent the next
    // init_swapchain() returns early and leaves the swapchain handle
    // null, which the driver dereferences on the next
    // vkAcquireNextImageKHR.
    m_surface_formats.clear();
    m_present_modes.clear();
    m_surface_format   = {.format = VK_FORMAT_UNDEFINED, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    m_present_mode     = VK_PRESENT_MODE_FIFO_KHR;
    m_image_count      = 0;
    m_swapchain_extent = {.width = 0, .height = 0};
    m_vulkan_swapchain = VK_NULL_HANDLE;
    m_is_valid         = false;
    m_is_empty         = true;

    // 4. Create a fresh VkSurfaceKHR over the (new) ANativeWindow that
    //    SDL has wired into the Context_window during foregrounding.
    void* new_surface = m_surface_create_info.context_window->create_vulkan_surface(static_cast<void*>(instance));
    if (new_surface == nullptr) {
        log_context->error("Surface_impl::recreate_for_new_window() create_vulkan_surface failed");
        return false;
    }
    m_surface = static_cast<VkSurfaceKHR>(new_surface);

    // 5. Re-run physical-device suitability + capability/format/present-mode
    //    queries on the new surface. The device chose this physical device
    //    at startup; we do not re-pick it here.
    if (cached_phys_dev != VK_NULL_HANDLE) {
        if (!use_physical_device(cached_phys_dev)) {
            log_context->error("Surface_impl::recreate_for_new_window() use_physical_device failed");
            return false;
        }
    }

    // 6. The Swapchain object stays the same instance; its
    //    Swapchain_impl is in a freshly-reset state. The next
    //    Swapchain_impl::wait_frame / init_swapchain pass creates the
    //    actual VkSwapchainKHR against the new VkSurfaceKHR. No frames
    //    are dropped here, and any Render_pass_impl::m_swapchain raw
    //    pointer that was cached before this call still points at the
    //    same valid Swapchain.

    log_context->info("Surface_impl::recreate_for_new_window() OK");
    return true;
}

auto Surface_impl::is_empty() -> bool
{
    return m_is_empty;
}

auto Surface_impl::is_valid() -> bool
{
    return m_is_valid;
}

namespace {

[[nodiscard]] auto to_erhe_surface_transform(VkSurfaceTransformFlagBitsKHR transform) -> Surface_transform
{
    switch (transform) {
        case VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR:   return Surface_transform::identity;
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:  return Surface_transform::rotate_90;
        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: return Surface_transform::rotate_180;
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: return Surface_transform::rotate_270;
        // Mirror / unknown transforms are not produced by Android display
        // rotation; present as-rendered (no pre-rotation).
        default:                                      return Surface_transform::identity;
    }
}

[[nodiscard]] auto c_str(VkSurfaceTransformFlagBitsKHR transform) -> const char*
{
    switch (transform) {
        case VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR:   return "IDENTITY";
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:  return "ROTATE_90";
        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: return "ROTATE_180";
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: return "ROTATE_270";
        default:                                      return "OTHER";
    }
}

} // anonymous namespace

auto Surface_impl::get_surface_transform() const -> Surface_transform
{
    return to_erhe_surface_transform(m_swapchain_transform);
}

auto Surface_impl::update_swapchain(Vulkan_swapchain_create_info& out_swapchain_create_info) -> bool
{
    log_context->debug("Surface_impl::configure_swapchain()");

    VkSurfaceCapabilitiesKHR surface_capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &surface_capabilities);
    m_is_valid = result == VK_SUCCESS;
    if (result != VK_SUCCESS) {
        // Demoted to debug: on Android the surface frequently goes into
        // a transient "lost" state during background / launch races,
        // and the editor's main loop retries every frame while the
        // window is not interactive. The first occurrence is logged
        // by the path that triggered it (e.g. vkQueuePresentKHR).
        log_context->debug("vkGetPhysicalDeviceSurfaceCapabilitiesKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        return false;
    }

    // Android pre-rotation: when the display panel's native orientation differs
    // from the (landscape-locked) app orientation, currentTransform is ROTATE_90
    // or ROTATE_270. We keep preTransform = currentTransform and render
    // pre-rotated, so for those transforms the swapchain images are created in
    // the surface's NATIVE orientation (width/height swapped vs the window's
    // logical size). See Imgui_renderer's clip-space rotation.
    const VkSurfaceTransformFlagBitsKHR current_transform   = surface_capabilities.currentTransform;
    const bool                          transform_swaps_wh  =
        (current_transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) ||
        (current_transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR);

    // Source the target extent from the window's pixel size rather than from
    // surface_capabilities.currentExtent. On Windows / Linux the two are
    // expected to match, but the window size is the authoritative
    // user-intended extent. On macOS with
    // SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE disabled (Vulkan path, see
    // sdl_window.cpp), surface_capabilities.currentExtent equals the last
    // CAMetalLayer.drawableSize MoltenVK set, i.e. the PREVIOUS swapchain's
    // imageExtent -- stale on resize. The window's SDL_GetWindowSizeInPixels
    // result reflects the new pixel size. If no Context_window is attached
    // (headless), fall back to currentExtent.
    VkExtent2D extent{.width = 0, .height = 0};
    if (m_surface_create_info.context_window != nullptr) {
        extent.width  = static_cast<uint32_t>(m_surface_create_info.context_window->get_width ());
        extent.height = static_cast<uint32_t>(m_surface_create_info.context_window->get_height());
    } else if (
        (surface_capabilities.currentExtent.width  == std::numeric_limits<uint32_t>::max()) ||
        (surface_capabilities.currentExtent.height == std::numeric_limits<uint32_t>::max())
    ) {
        extent.width  = 0;
        extent.height = 0;
    } else {
        extent.width  = surface_capabilities.currentExtent.width;
        extent.height = surface_capabilities.currentExtent.height;
    }
    // Swap to native orientation for a 90/270 transform (see comment above). Done
    // before clamping so the clamp uses the surface's native min/max extents.
    if (transform_swaps_wh) {
        const uint32_t swap_tmp = extent.width;
        extent.width  = extent.height;
        extent.height = swap_tmp;
    }
    log_context->info(
        "  Surface transform : {} ; swapchain native extent : {} x {} (currentExtent : {} x {})",
        c_str(current_transform),
        extent.width, extent.height,
        surface_capabilities.currentExtent.width, surface_capabilities.currentExtent.height
    );
    log_context->debug("  Surface min extent : {} x {}", surface_capabilities.minImageExtent.width, surface_capabilities.minImageExtent.height);
    log_context->debug("  Surface max extent : {} x {}", surface_capabilities.maxImageExtent.width, surface_capabilities.maxImageExtent.height);

    // Detect empty-surface conditions before clamping. A zero-width or
    // zero-height window (minimized / shaded / otherwise hidden) must skip
    // swapchain recreation; otherwise we'd clamp to the driver minimum
    // (usually 1x1) and render into something the user cannot see.
    m_is_empty =
        (extent.width  == 0) ||
        (extent.height == 0) ||
        (surface_capabilities.currentExtent.width  == 0) ||
        (surface_capabilities.currentExtent.height == 0);

    if (m_is_empty) {
        extent = VkExtent2D{.width = 0, .height = 0};
        return false;
    }

    // Clamp swapchain size
    extent.width  = std::min(extent.width,  surface_capabilities.maxImageExtent.width);
    extent.height = std::min(extent.height, surface_capabilities.maxImageExtent.height);
    extent.width  = std::max(extent.width,  surface_capabilities.minImageExtent.width);
    extent.height = std::max(extent.height, surface_capabilities.minImageExtent.height);

    // Recreate when the native extent changed OR the surface transform changed.
    // A LandscapeLeft <-> LandscapeRight flip keeps the native extent identical
    // but flips the transform between ROTATE_90 and ROTATE_270, which still needs
    // a new swapchain (and a different pre-rotation in the renderer).
    if (
        (extent.width      == m_swapchain_extent.width ) &&
        (extent.height     == m_swapchain_extent.height) &&
        (current_transform == m_swapchain_transform)
    ) {
        return false;
    }

    uint32_t graphics_queue_family_index = m_device_impl.get_graphics_queue_family_index();
    bool     use_scaling                 = m_device_impl.get_capabilities().m_surface_maintenance1;

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

            // VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR does not work on AMD / Windows
            const bool need_workaround_for_aspect_ratio_stretch =
#if defined(ERHE_OS_WINDOWS)
                m_device_impl.get_info().vendor == Vendor::Amd;
#else
                false;
#endif
            if (aspect_ratio_stretch && !need_workaround_for_aspect_ratio_stretch) {
                scaling_behavior = VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR;
            } else if (stretch) {
                scaling_behavior = VK_PRESENT_SCALING_STRETCH_BIT_KHR;
            } else if (one_to_one) {
                scaling_behavior = VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR;
            }

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

    // Build the pNext chain for the swapchain create info.
    // When swapchain_maintenance1 is enabled, provide VkSwapchainPresentModesCreateInfoKHR
    // to inform the implementation which present modes the application may use.
    // Per VUID-VkSwapchainPresentModesCreateInfoKHR-pPresentModes-07763, each entry must be
    // compatible with the chosen presentMode as returned by VkSurfacePresentModeCompatibilityKHR.
    // We never switch present modes at runtime via VkSwapchainPresentModeInfoKHR, so listing
    // the chosen mode (trivially compatible with itself) is sufficient and always correct.
    bool use_present_modes = m_device_impl.get_capabilities().m_swapchain_maintenance1;

    out_swapchain_create_info.present_modes = {m_present_mode};
    out_swapchain_create_info.swapchain_present_modes_create_info = VkSwapchainPresentModesCreateInfoKHR{
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_KHR, // VkStructureType
        .pNext            = nullptr,                                                   // const void*
        .presentModeCount = static_cast<uint32_t>(out_swapchain_create_info.present_modes.size()), // uint32_t
        .pPresentModes    = out_swapchain_create_info.present_modes.data()              // const VkPresentModeKHR*
    };

    out_swapchain_create_info.swapchain_present_scaling_create_info = VkSwapchainPresentScalingCreateInfoKHR{
        .sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_KHR, // VkStructureType
        .pNext           = use_present_modes                                            // const void*
                              ? &out_swapchain_create_info.swapchain_present_modes_create_info
                              : nullptr,
        .scalingBehavior = scaling_behavior,                                            // VkPresentScalingFlagsKHR
        .presentGravityX = gravity_x,                                                   // VkPresentGravityFlagsKHR
        .presentGravityY = gravity_y                                                    // VkPresentGravityFlagsKHR
    };

    // Chain: swapchain_create_info -> [scaling -> [present_modes]] or [present_modes]
    const void* pNext = nullptr;
    if (use_scaling) {
        pNext = &out_swapchain_create_info.swapchain_present_scaling_create_info;
    } else if (use_present_modes) {
        pNext = &out_swapchain_create_info.swapchain_present_modes_create_info;
    }

    out_swapchain_create_info.swapchain_create_info = VkSwapchainCreateInfoKHR{
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,   // VkStructureType
        .pNext                 = pNext,                                         // const void*
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
        .oldSwapchain          = nullptr                                        // VkSwapchainKHR
    };
    // Record the extent the caller will use to recreate the swapchain so the
    // next call's "extent unchanged" early-out (above) actually fires.
    // Without this, m_swapchain_extent stays at {0,0} and every
    // request_resize pass rebuilds the swapchain even when the size did not
    // change.
    m_swapchain_extent    = extent;
    m_swapchain_transform = current_transform;
    return true;
}

} // namespace erhe::graphics
