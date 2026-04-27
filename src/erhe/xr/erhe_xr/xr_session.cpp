#include "erhe_xr/xr_session.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/native_format.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"

#include "erhe_xr/xr.hpp"
#include "erhe_xr/xr_instance.hpp"
#include "erhe_xr/xr_log.hpp"
#include "erhe_xr/xr_swapchain_image.hpp"

#include <fmt/format.h>

#if defined(ERHE_OS_WINDOWS)
#   include <unknwn.h>
#endif

// volk.h must precede <openxr/openxr_platform.h> when XR_USE_GRAPHICS_API_VULKAN
// is defined: openxr_platform.h references VkFormat / VkImage / etc. without
// including <vulkan/vulkan.h> itself.
#if defined(XR_USE_GRAPHICS_API_VULKAN)
# include "volk.h"
#endif

//#if defined(_MSC_VER) && !defined(__clang__)
//#   pragma warning(push)
//#   pragma warning(disable : 26812) // The enum type is unscoped. Prefer ‘enum class’ over ‘enum’ (Enum.3).
//#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#if defined(ERHE_OS_LINUX)
# include <wayland-client.h>
#endif

//#if defined(_MSC_VER) && !defined(__clang__)
//#   pragma warning(pop)
//#endif

namespace erhe::xr {

Xr_session::Xr_session(
    Xr_instance&                  instance,
    erhe::window::Context_window& context_window,
    erhe::graphics::Device&       graphics_device,
    bool                          mirror_mode
)
    : m_instance                      {instance}
    , m_context_window                {context_window}
    , m_graphics_device               {graphics_device}
    , m_mirror_mode                   {mirror_mode}
    , m_swapchain_color_format        {erhe::dataformat::Format::format_undefined}
    , m_swapchain_depth_stencil_format{erhe::dataformat::Format::format_undefined}
    , m_xr_frame_state                {XR_TYPE_FRAME_STATE, nullptr, 0, 0, XR_FALSE}
{
    ERHE_PROFILE_FUNCTION();

    if (instance.get_xr_instance() == XR_NULL_HANDLE) {
        log_xr->error("No XR Instance");
        return;
    }

    m_xr_views.resize(instance.get_xr_view_configuration_views().size());
    for (auto& view : m_xr_views) {
        view = XrView{
            .type = XR_TYPE_VIEW,
            .next = nullptr,
            .pose = {
                .orientation = {
                    .x = 0.0f,
                    .y = 0.0f,
                    .z = 0.0f,
                    .w = 1.0f
                },
                .position = {
                    .x = 0.0f,
                    .y = 0.0f,
                    .z = 0.0f
                },
            },
            .fov = {
                .angleLeft  = 0.0f,
                .angleRight = 0.0f,
                .angleUp    = 0.0f,
                .angleDown  = 0.0f
            }
        };
    }

    if (!create_session()) {
        return;
    }
    if (!enumerate_swapchain_formats()) {
        return;
    }
    if (!enumerate_reference_spaces()) {
        return;
    }
    if (!create_swapchains()) {
        return;
    }
    if (!create_reference_space()) {
        return;
    }
    if (!attach_actions()) {
        return;
    }
    if (!create_hand_tracking()) {
        return;
    }
}

auto Xr_session::create_session() -> bool
{
    ERHE_PROFILE_FUNCTION();

    log_xr->trace("{}", __func__);

    const auto xr_instance = m_instance.get_xr_instance();
    ERHE_VERIFY(xr_instance != XR_NULL_HANDLE);

#if defined(XR_USE_GRAPHICS_API_VULKAN)
    XrGraphicsRequirementsVulkan2KHR xr_graphics_requirements_vulkan{
        .type                   = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR,
        .next                   = nullptr,
        .minApiVersionSupported = 0,
        .maxApiVersionSupported = 0
    };
    if (m_instance.xrGetVulkanGraphicsRequirements2KHR != nullptr) {
        ERHE_XR_CHECK(m_instance.xrGetVulkanGraphicsRequirements2KHR(xr_instance, m_instance.get_xr_system_id(), &xr_graphics_requirements_vulkan));
        const uint16_t vk_min_major = XR_VERSION_MAJOR(xr_graphics_requirements_vulkan.minApiVersionSupported);
        const uint16_t vk_min_minor = XR_VERSION_MINOR(xr_graphics_requirements_vulkan.minApiVersionSupported);
        const uint16_t vk_max_major = XR_VERSION_MAJOR(xr_graphics_requirements_vulkan.maxApiVersionSupported);
        const uint16_t vk_max_minor = XR_VERSION_MINOR(xr_graphics_requirements_vulkan.maxApiVersionSupported);
        log_xr->info("Vulkan minApiVersionSupported = {}.{}", vk_min_major, vk_min_minor);
        log_xr->info("Vulkan maxApiVersionSupported = {}.{}", vk_max_major, vk_max_minor);
    }
#endif

#if defined(XR_USE_GRAPHICS_API_OPENGL)
    XrGraphicsRequirementsOpenGLKHR xr_graphics_requirements_opengl{
        .type                   = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR,
        .next                   = nullptr,
        .minApiVersionSupported = 0,
        .maxApiVersionSupported = 0
    };

    ERHE_XR_CHECK(m_instance.xrGetOpenGLGraphicsRequirementsKHR(xr_instance, m_instance.get_xr_system_id(), &xr_graphics_requirements_opengl));
    const uint16_t min_major = (xr_graphics_requirements_opengl.minApiVersionSupported >> 48) & uint64_t{0x0000ffffu};
    const uint16_t min_minor = (xr_graphics_requirements_opengl.minApiVersionSupported >> 32) & uint64_t{0x0000ffffu};
    const uint16_t max_major = (xr_graphics_requirements_opengl.maxApiVersionSupported >> 48) & uint64_t{0x0000ffffu};
    const uint16_t max_minor = (xr_graphics_requirements_opengl.maxApiVersionSupported >> 32) & uint64_t{0x0000ffffu};

    log_xr->info("OpenGL minApiVersionSupported = {}.{}", min_major, min_minor);
    log_xr->info("OpenGL maxApiVersionSupported = {}.{}", max_major, max_minor);
#endif

    const erhe::graphics::Native_device_handles native_handles = m_graphics_device.get_native_handles();

#if defined(XR_USE_GRAPHICS_API_VULKAN)
    const XrGraphicsBindingVulkan2KHR graphics_binding_vulkan2{
        .type             = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR,
        .next             = nullptr,
        .instance         = reinterpret_cast<VkInstance>(native_handles.vk_instance),
        .physicalDevice   = reinterpret_cast<VkPhysicalDevice>(native_handles.vk_physical_device),
        .device           = reinterpret_cast<VkDevice>(native_handles.vk_device),
        .queueFamilyIndex = native_handles.vk_queue_family_index,
        .queueIndex       = native_handles.vk_queue_index
    };

    const XrSessionCreateInfo session_create_info{
        .type        = XR_TYPE_SESSION_CREATE_INFO,
        .next        = reinterpret_cast<const XrBaseInStructure*>(&graphics_binding_vulkan2),
        .createFlags = 0,
        .systemId    = m_instance.get_xr_system_id()
    };
    ERHE_XR_CHECK(xrCreateSession(xr_instance, &session_create_info, &m_xr_session));
#elif defined(XR_USE_PLATFORM_WIN32)
# if defined(XR_USE_GRAPHICS_API_OPENGL)
    const XrGraphicsBindingOpenGLWin32KHR graphics_binding_opengl_win32{
        .type  = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR,
        .next  = nullptr,
        .hDC   = reinterpret_cast<HDC>(native_handles.gl_hdc),
        .hGLRC = reinterpret_cast<HGLRC>(native_handles.gl_hglrc)
    };

    const XrSessionCreateInfo session_create_info{
        .type        = XR_TYPE_SESSION_CREATE_INFO,
        .next        = reinterpret_cast<const XrBaseInStructure*>(&graphics_binding_opengl_win32),
        .createFlags = 0,
        .systemId    = m_instance.get_xr_system_id()
    };

    check_gl_context_in_current_in_this_thread();
    ERHE_XR_CHECK(xrCreateSession(xr_instance, &session_create_info, &m_xr_session));
# endif
#elif defined(XR_USE_PLATFORM_LINUX)
# if defined(XR_USE_GRAPHICS_API_OPENGL)
    const XrGraphicsBindingOpenGLWaylandKHR graphics_binding_opengl_wayland{
        .type    = XR_TYPE_GRAPHICS_BINDING_OPENGL_WAYLAND_KHR,
        .next    = nullptr,
        .display = reinterpret_cast<struct wl_display*>(native_handles.gl_wl_display)
    };

    const XrSessionCreateInfo session_create_info{
        .type        = XR_TYPE_SESSION_CREATE_INFO,
        .next        = reinterpret_cast<const XrBaseInStructure*>(&graphics_binding_opengl_wayland),
        .createFlags = 0,
        .systemId    = m_instance.get_xr_system_id()
    };

    check_gl_context_in_current_in_this_thread();
    ERHE_XR_CHECK(xrCreateSession(xr_instance, &session_create_info, &m_xr_session));
# endif
#else
    static_cast<void>(native_handles);
    // TODO
    abort();
#endif

    if (m_xr_session == XR_NULL_HANDLE) {
        log_xr->error("xrCreateSession() created XR_NULL_HANDLE session");
        return false;
    }

    if (m_instance.extensions.FB_color_space) {
        uint32_t count{0};
        XrResult enumerate_color_spaces_result = m_instance.xrEnumerateColorSpacesFB(m_xr_session, 0, &count, nullptr);
        if ((enumerate_color_spaces_result == XR_SUCCESS) && (count > 0)) {
            std::vector<XrColorSpaceFB> color_spaces(count);
            uint32_t count2 = count;
            enumerate_color_spaces_result = m_instance.xrEnumerateColorSpacesFB(m_xr_session, count, &count2, color_spaces.data());
            if (enumerate_color_spaces_result == XR_SUCCESS) {
                log_xr->info("Session color spaces:");
                XrColorSpaceFB best_color_space = color_spaces.front();
                int best_score = 0;
                for (uint32_t i = 0; i < count; ++i) {
                    const XrColorSpaceFB color_space = color_spaces[i];
                    log_xr->info("    {}", c_str(color_space));
                    int score = color_space_score(color_space);
                    if (score > best_score) {
                        best_score = score;
                        best_color_space = color_space;
                    }
                }
                //best_color_space = XR_COLOR_SPACE_UNMANAGED_FB;
                //best_color_space = XR_COLOR_SPACE_REC2020_FB;
                //best_color_space = XR_COLOR_SPACE_REC709_FB;
                //best_color_space = XR_COLOR_SPACE_RIFT_CV1_FB;
                //best_color_space = XR_COLOR_SPACE_RIFT_S_FB;
                //best_color_space = XR_COLOR_SPACE_QUEST_FB;
                //best_color_space = XR_COLOR_SPACE_P3_FB;
                //best_color_space = XR_COLOR_SPACE_ADOBE_RGB_FB;
                XrResult set_color_space_result = m_instance.xrSetColorSpaceFB(m_xr_session, best_color_space);
                if (set_color_space_result == XR_SUCCESS) {
                    log_xr->info("Selected session color space: {}", c_str(best_color_space));
                } else {
                    log_xr->warn("xrSetColorSpaceFB({}) failed with error {}", c_str(best_color_space), c_str(set_color_space_result));
                }
            }
        }
        if (enumerate_color_spaces_result != XR_SUCCESS) {
            log_xr->warn("xrEnumerateColorSpacesFB() failed with error {}", c_str(enumerate_color_spaces_result));
        }
    }

    return true;
}

Xr_session::~Xr_session() noexcept
{
    log_xr->trace("{}", __func__);

    if (m_instance.xrDestroyHandTrackerEXT != nullptr) {
        if (m_hand_tracker_left.hand_tracker != XR_NULL_HANDLE) {
            m_instance.xrDestroyHandTrackerEXT(m_hand_tracker_left.hand_tracker);
        }
        if (m_hand_tracker_right.hand_tracker != XR_NULL_HANDLE) {
            m_instance.xrDestroyHandTrackerEXT(m_hand_tracker_right.hand_tracker);
        }
    }

    m_view_swapchains.clear();

    if (m_xr_session == XR_NULL_HANDLE) {
        return;
    }

    xrRequestExitSession(m_xr_session);
    while (m_session_running) {
        const bool ok = m_instance.poll_xr_events(*this); // This will eventually call end_session() -> xrEndSession()
        if (!ok) {
            log_xr->warn("OpenXR error polling events after xrRequestExitSession()");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    check_gl_context_in_current_in_this_thread();
    check(xrDestroySession(m_xr_session));
}

auto Xr_session::get_xr_session() const -> XrSession
{
    return m_xr_session;
}

auto Xr_session::get_xr_reference_space_local() const -> XrSpace
{
    return m_xr_reference_space_local;
}

auto Xr_session::get_xr_reference_space_stage() const -> XrSpace
{
    return m_xr_reference_space_stage;
}

auto Xr_session::get_xr_reference_space_view() const -> XrSpace
{
    return m_xr_reference_space_view;
}

auto Xr_session::get_xr_frame_state() const -> const XrFrameState&
{
    return m_xr_frame_state;
}

auto Xr_session::color_space_score(const XrColorSpaceFB color_space) const -> int
{
    // TODO
    switch (color_space) {
        case XR_COLOR_SPACE_UNMANAGED_FB: return 1;
        case XR_COLOR_SPACE_REC2020_FB:   return 1;
        case XR_COLOR_SPACE_REC709_FB:    return 1;
        case XR_COLOR_SPACE_RIFT_CV1_FB:  return 2;
        case XR_COLOR_SPACE_RIFT_S_FB:    return 1;
        case XR_COLOR_SPACE_QUEST_FB:     return 1;
        case XR_COLOR_SPACE_P3_FB:        return 1;
        case XR_COLOR_SPACE_ADOBE_RGB_FB: return 1;
        default:                          return 0;
    }
}

auto Xr_session::color_format_score(const erhe::dataformat::Format pixelformat) const -> int
{
    switch (pixelformat) {
        //using enum gl::Internal_format;
        case erhe::dataformat::Format::format_8_vec4_unorm:             return 1;
        case erhe::dataformat::Format::format_8_vec4_srgb:              return 0; // wrong color space in quest
        case erhe::dataformat::Format::format_packed1010102_vec4_unorm: return 3;
        case erhe::dataformat::Format::format_packed111110_vec3_unorm:  return 4;
        case erhe::dataformat::Format::format_16_vec3_float:            return 6;
        case erhe::dataformat::Format::format_16_vec4_float:            return 5;
        default:                                                        return 0;
    }
}

auto Xr_session::depth_stencil_format_score(const erhe::dataformat::Format pixelformat) const -> int
{
#if defined(XR_USE_GRAPHICS_API_OPENGL)
    switch (pixelformat) {
        //using enum gl::Internal_format;
        case erhe::dataformat::Format::format_s8_uint:             return 0;
        case erhe::dataformat::Format::format_d16_unorm:           return 1;
        case erhe::dataformat::Format::format_x8_d24_unorm_pack32: return 2;
        case erhe::dataformat::Format::format_d32_sfloat:          return 3;
        case erhe::dataformat::Format::format_d24_unorm_s8_uint:   return 4;
        case erhe::dataformat::Format::format_d32_sfloat_s8_uint:  return 5;
        default:                                                   return 0;
    }
#endif
#if defined(XR_USE_GRAPHICS_API_VULKAN)
    // Vulkan does not meet DS+SAMPLED+TSRC+TDST+STORAGE requirements, only DS+SAMPLED
    switch (pixelformat) {
        //using enum gl::Internal_format;
        case erhe::dataformat::Format::format_s8_uint:             return 0;
        case erhe::dataformat::Format::format_d16_unorm:           return 1;
        case erhe::dataformat::Format::format_x8_d24_unorm_pack32: return 0; // runtime requirements not met
        case erhe::dataformat::Format::format_d32_sfloat:          return 3;
        case erhe::dataformat::Format::format_d24_unorm_s8_uint:   return 0; // runtime requirements not met
        case erhe::dataformat::Format::format_d32_sfloat_s8_uint:  return 0; // runtime requirements not met
        default:                                                   return 0;
    }
#endif
}

auto Xr_session::enumerate_swapchain_formats() -> bool
{
    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }

    uint32_t swapchain_format_count{0};

    ERHE_XR_CHECK(xrEnumerateSwapchainFormats(m_xr_session, 0, &swapchain_format_count, nullptr));

    std::vector<int64_t> swapchain_formats(swapchain_format_count);

    ERHE_XR_CHECK(
        xrEnumerateSwapchainFormats(m_xr_session, swapchain_format_count, &swapchain_format_count, swapchain_formats.data())
    );

    log_xr->info("Swapchain formats:");
    int best_color_format_score{0};
    int best_depth_stencil_score{0};
    std::vector<erhe::dataformat::Format> depth_candidates;
    for (const int64_t swapchain_format : swapchain_formats) {
        const erhe::dataformat::Format pixelformat = erhe::graphics::native_swapchain_format_to_dataformat(swapchain_format);
        const std::string              native_name = erhe::graphics::native_swapchain_format_c_str(swapchain_format);
        if (pixelformat == erhe::dataformat::Format::format_undefined) {
            log_xr->info("    {} (unmapped)", native_name);
            continue;
        }
        log_xr->info("    {} = {}", native_name, erhe::dataformat::c_str(pixelformat));
        const int color_score         = color_format_score(pixelformat);
        const int depth_stencil_score = depth_stencil_format_score(pixelformat);
        if (color_score > best_color_format_score) {
            best_color_format_score = color_score;
            m_swapchain_color_format = pixelformat;
        }
        if (depth_stencil_score > best_depth_stencil_score) {
            best_depth_stencil_score = depth_stencil_score;
            m_swapchain_depth_stencil_format = pixelformat;
        }
        if (depth_stencil_score > 0) {
            depth_candidates.push_back(pixelformat);
        }
    }

    // Diagnostic: probe each depth-capable format from the XR format list
    // against the usage-mask superset we have seen the runtime actually
    // expand to internally on Vulkan. On GL, probe_image_format_support
    // returns true unconditionally so every entry reports PASS. See
    // doc/vulkan_backend.md log for context.
    {
        constexpr uint64_t full_usage_mask =
            erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
            erhe::graphics::Image_usage_flag_bit_mask::sampled                  |
            erhe::graphics::Image_usage_flag_bit_mask::transfer_src             |
            erhe::graphics::Image_usage_flag_bit_mask::transfer_dst             |
            erhe::graphics::Image_usage_flag_bit_mask::storage;
        constexpr uint64_t min_usage_mask =
            erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
            erhe::graphics::Image_usage_flag_bit_mask::sampled;

        log_xr->info(
            "Depth format probes (usage: full = DS+SAMPLED+TSRC+TDST+STORAGE, min = DS+SAMPLED):"
        );
        std::size_t full_pass_count = 0;
        std::size_t min_pass_count  = 0;
        for (const erhe::dataformat::Format candidate : depth_candidates) {
            const bool full_ok = m_graphics_device.probe_image_format_support(candidate, full_usage_mask);
            const bool min_ok  = m_graphics_device.probe_image_format_support(candidate, min_usage_mask);
            if (full_ok) { ++full_pass_count; }
            if (min_ok)  { ++min_pass_count; }
            log_xr->info(
                "    {} -> full={} min={}",
                erhe::dataformat::c_str(candidate),
                full_ok ? "PASS" : "FAIL",
                min_ok  ? "PASS" : "FAIL"
            );
        }
        log_xr->info(
            "Depth probe summary: {}/{} pass full usage, {}/{} pass min usage",
            full_pass_count, depth_candidates.size(),
            min_pass_count,  depth_candidates.size()
        );
    }
    log_xr->info("Selected swapchain color format {}{}", erhe::dataformat::c_str(m_swapchain_color_format), m_mirror_mode ? " (mirror mode enabled)" : "");
    log_xr->info("Selected swapchain depth stencil format {}", erhe::dataformat::c_str(m_swapchain_depth_stencil_format));

    return true;
}

auto Xr_session::enumerate_reference_spaces() -> bool
{
    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }

    uint32_t reference_space_type_count{0};
    ERHE_XR_CHECK(
        xrEnumerateReferenceSpaces(m_xr_session, 0, &reference_space_type_count, nullptr)
    );

    m_xr_reference_space_types.resize(reference_space_type_count);
    ERHE_XR_CHECK(
        xrEnumerateReferenceSpaces(m_xr_session, reference_space_type_count, &reference_space_type_count, m_xr_reference_space_types.data())
    );
    log_xr->info("Reference space types:");
    for (const auto reference_space_type : m_xr_reference_space_types) {
        log_xr->info("    {}", c_str(reference_space_type));
    }

    return true;
}

auto Xr_session::create_swapchains() -> bool
{
    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }

    m_view_swapchains.clear();

    const int64_t native_color_format         = erhe::graphics::dataformat_to_native_swapchain_format(m_swapchain_color_format);
    const int64_t native_depth_stencil_format = erhe::graphics::dataformat_to_native_swapchain_format(m_swapchain_depth_stencil_format);
    ERHE_VERIFY(native_color_format != 0);

    constexpr uint64_t color_texture_usage =
        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
        erhe::graphics::Image_usage_flag_bit_mask::sampled;
    constexpr uint64_t depth_stencil_texture_usage =
        erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
        erhe::graphics::Image_usage_flag_bit_mask::sampled;

    // On Vulkan the depth swapchain creation is currently disabled (see
    // doc/vulkan_backend.md). On GL the runtime accepts the depth format
    // and the editor uses the resulting depth texture for the projection
    // layer's depth info.
#if defined(XR_USE_GRAPHICS_API_VULKAN)
    constexpr bool create_depth_stencil_swapchain = true;
#else
    constexpr bool create_depth_stencil_swapchain = true;
#endif

    const auto& views = m_instance.get_xr_view_configuration_views();
    for (uint32_t view_index = 0; view_index < views.size(); ++view_index) {
        const auto& view = views[view_index];
        const XrSwapchainCreateInfo color_swapchain_create_info{
            .type        = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .next        = nullptr,
            .createFlags = 0,
            .usageFlags  = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
            .format      = native_color_format,
            .sampleCount = view.recommendedSwapchainSampleCount,
            .width       = view.recommendedImageRectWidth,
            .height      = view.recommendedImageRectHeight,
            .faceCount   = 1,
            .arraySize   = 1,
            .mipCount    = 1
        };

        XrSwapchain color_xr_swapchain{XR_NULL_HANDLE};
        check_gl_context_in_current_in_this_thread();
        const XrResult color_result = xrCreateSwapchain(m_xr_session, &color_swapchain_create_info, &color_xr_swapchain);
        if (color_result != XR_SUCCESS) {
            log_xr->error("xrCreateSwapchain() failed with error {}", c_str(color_result));
            return false;
        }

        XrSwapchain depth_stencil_xr_swapchain{XR_NULL_HANDLE};
        if (create_depth_stencil_swapchain && (native_depth_stencil_format != 0)) {
            const XrSwapchainCreateInfo depth_stencil_swapchain_create_info{
                .type        = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                .next        = nullptr,
                .createFlags = 0,
                .usageFlags  = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .format      = native_depth_stencil_format,
                .sampleCount = view.recommendedSwapchainSampleCount,
                .width       = view.recommendedImageRectWidth,
                .height      = view.recommendedImageRectHeight,
                .faceCount   = 1,
                .arraySize   = 1,
                .mipCount    = 1
            };
            check_gl_context_in_current_in_this_thread();
            const XrResult depth_stencil_result = xrCreateSwapchain(m_xr_session, &depth_stencil_swapchain_create_info, &depth_stencil_xr_swapchain);
            if (depth_stencil_result != XR_SUCCESS) {
                log_xr->error("xrCreateSwapchain() failed with error {}", c_str(depth_stencil_result));
                return false;
            }
        }

        m_view_swapchains.emplace_back(
            Swapchain{
                m_graphics_device,
                color_xr_swapchain,
                m_swapchain_color_format,
                view.recommendedImageRectWidth,
                view.recommendedImageRectHeight,
                view.recommendedSwapchainSampleCount,
                color_texture_usage,
                fmt::format("XR color view {}", view_index)
            },
            Swapchain{
                m_graphics_device,
                depth_stencil_xr_swapchain,
                m_swapchain_depth_stencil_format,
                view.recommendedImageRectWidth,
                view.recommendedImageRectHeight,
                view.recommendedSwapchainSampleCount,
                depth_stencil_texture_usage,
                fmt::format("XR depth stencil view {}", view_index)
            }
        );
    }

    if (m_instance.extensions.FB_passthrough) {
        XrPassthroughCreateInfoFB passthrough_create_info{
            .type  = XR_TYPE_PASSTHROUGH_CREATE_INFO_FB,
            .next  = nullptr,
            .flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB
        };

        XrResult passthrough_result = m_instance.xrCreatePassthroughFB(m_xr_session, &passthrough_create_info, &m_passthrough_fb);
        if (passthrough_result != XR_SUCCESS) {
            log_xr->warn("xrCreatePassthroughFB() failed with error {}", c_str(passthrough_result));
        } else {
            XrPassthroughLayerCreateInfoFB layer_create_info{
                .type        = XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB,
                .next        = nullptr,
                .passthrough = m_passthrough_fb,
                .flags       = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB,
                .purpose     = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB
            };
            passthrough_result = m_instance.xrCreatePassthroughLayerFB(m_xr_session, &layer_create_info, &m_passthrough_layer_fb);
        }

        if (passthrough_result != XR_SUCCESS) {
            log_xr->warn("xrCreatePassthroughLayerFB() failed with error {}", c_str(passthrough_result));
        } else {
            passthrough_result = m_instance.xrPassthroughStartFB(m_passthrough_fb);
        }

        // We already use XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB so this is not needed
        // if (passthrough_result != XR_SUCCESS) {
        //     log_xr->warn("xrPassthroughStartFB() failed with error {}", c_str(passthrough_result));
        // } else {
        //     m_instance.xrPassthroughLayerResumeFB(m_passthrough_layer_fb);
        // }

        if (passthrough_result != XR_SUCCESS) {
            log_xr->warn("xrPassthroughLayerResumeFB() failed with error {}", c_str(passthrough_result));
        } else {
            log_xr->info("Initialized XR_FB_passthrough");
        }
    }

    return true;
}

auto Xr_session::create_reference_space() -> bool
{
    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }

    XrReferenceSpaceCreateInfo local_create_info{
        .type                 = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .next                 = nullptr,
        .referenceSpaceType   = XR_REFERENCE_SPACE_TYPE_LOCAL,
        .poseInReferenceSpace = {
            .orientation = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f,
                .w = 1.0f
            },
            .position = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f
            }
        }
    };

    XrReferenceSpaceCreateInfo stage_create_info{
        .type                 = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .next                 = nullptr,
        .referenceSpaceType   = XR_REFERENCE_SPACE_TYPE_STAGE,
        .poseInReferenceSpace = {
            .orientation = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f,
                .w = 1.0f
            },
            .position = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f
            }
        }
    };

    XrReferenceSpaceCreateInfo view_create_info{
        .type                 = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .next                 = nullptr,
        .referenceSpaceType   = XR_REFERENCE_SPACE_TYPE_VIEW,
        .poseInReferenceSpace = {
            .orientation = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f,
                .w = 1.0f
            },
            .position = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f
            }
        }
    };

    ERHE_XR_CHECK(xrCreateReferenceSpace(m_xr_session, &local_create_info, &m_xr_reference_space_local));
    ERHE_XR_CHECK(xrCreateReferenceSpace(m_xr_session, &stage_create_info, &m_xr_reference_space_stage));
    ERHE_XR_CHECK(xrCreateReferenceSpace(m_xr_session, &view_create_info, &m_xr_reference_space_view));

    return true;
}

auto Xr_session::begin_session() -> bool
{
    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }

    XrSessionBeginInfo session_begin_info{
        .type                         = XR_TYPE_SESSION_BEGIN_INFO,
        .next                         = nullptr,
        .primaryViewConfigurationType = m_instance.get_xr_view_configuration_type()
    };

    log_xr->info("xrBeginSession()");
    ERHE_XR_CHECK(xrBeginSession(m_xr_session, &session_begin_info));

    // m_instance.set_environment_depth_estimation(m_session, true);

    m_session_running = true;
    return true;
}

auto Xr_session::end_session() -> bool
{
    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }

    log_xr->info("xrEndSession()");
    ERHE_XR_CHECK(xrEndSession(m_xr_session));
    m_session_running = false;
    return true;
}

auto Xr_session::is_session_running() const -> bool
{
    return m_session_running;
}

auto Xr_session::attach_actions() -> bool
{
    if (m_xr_session == XR_NULL_HANDLE) {
        log_xr->warn("Session has no instance");
        return false;
    }

    return m_instance.attach_actions(m_xr_session);
}

auto Xr_session::create_hand_tracking() -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }

    if (m_instance.xrCreateHandTrackerEXT == nullptr) {
        return false;
    }

    const XrHandTrackerCreateInfoEXT left_hand{
        .type         = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
        .next         = nullptr,
        .hand         = XR_HAND_LEFT_EXT,
        .handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT
    };
    const XrHandTrackerCreateInfoEXT right_hand{
        .type         = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
        .next         = nullptr,
        .hand         = XR_HAND_RIGHT_EXT,
        .handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT
    };

    ERHE_XR_CHECK(m_instance.xrCreateHandTrackerEXT(m_xr_session, &left_hand,  &m_hand_tracker_left.hand_tracker));
    ERHE_XR_CHECK(m_instance.xrCreateHandTrackerEXT(m_xr_session, &right_hand, &m_hand_tracker_right.hand_tracker));

    m_hand_tracker_left.velocities = {
        .type            = XR_TYPE_HAND_JOINT_VELOCITIES_EXT,
        .next            = nullptr,
        .jointCount      = XR_HAND_JOINT_COUNT_EXT,
        .jointVelocities = &m_hand_tracker_left.joint_velocities[0]
    };
    m_hand_tracker_left.locations = {
        .type           = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
        .next           = &m_hand_tracker_left.velocities,
        .isActive       = XR_FALSE,
        .jointCount     = XR_HAND_JOINT_COUNT_EXT,
        .jointLocations = &m_hand_tracker_left.joint_locations[0]
    };

    m_hand_tracker_right.velocities = {
        .type            = XR_TYPE_HAND_JOINT_VELOCITIES_EXT,
        .next            = nullptr,
        .jointCount      = XR_HAND_JOINT_COUNT_EXT,
        .jointVelocities = &m_hand_tracker_right.joint_velocities[0]
    };
    m_hand_tracker_right.locations = {
        .type           = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
        .next           = &m_hand_tracker_right.velocities,
        .isActive       = XR_FALSE,
        .jointCount     = XR_HAND_JOINT_COUNT_EXT,
        .jointLocations = &m_hand_tracker_right.joint_locations[0]
    };

    update_hand_tracking();
    return true;
}

void Xr_session::update_view_pose()
{
    XrSpaceLocation location{
        .type          = XR_TYPE_SPACE_LOCATION,
        .next          = nullptr,
        .locationFlags = 0,
        .pose = {
            .orientation = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f,
                .w = 1.0f
            },
            .position = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f
            }
        }
    };

    const XrResult result = xrLocateSpace(
        m_xr_reference_space_view,
        //m_xr_reference_space_local,
        m_xr_reference_space_stage,
        m_xr_frame_state.predictedDisplayTime,
        &location
    );

    if ((result == XR_SUCCESS) || (result == XR_SESSION_LOSS_PENDING)) {
        m_view_location = location;
    } else {
        log_xr->warn("xrLocateSpace() failed with error {}", c_str(result));
    }
}

void Xr_session::update_hand_tracking()
{
    ERHE_PROFILE_FUNCTION();

    if (m_instance.xrLocateHandJointsEXT == nullptr) {
        return;
    }

#if 0
    XrHandJointsLocateInfoEXT leftHandJointsLocateInfo{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
    XrHandJointLocationsEXT   leftHandLocations       {XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
    XrHandJointLocationsEXT   rightHandLocations      {XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
    XrHandJointVelocitiesEXT  leftHandVelocities      {XR_TYPE_HAND_JOINT_VELOCITIES_EXT};
    XrHandJointVelocitiesEXT  rightHandVelocities     {XR_TYPE_HAND_JOINT_VELOCITIES_EXT};
    XrHandJointLocationEXT    leftHandLocation [XR_HAND_JOINT_COUNT_EXT];
    XrHandJointLocationEXT    rightHandLocation[XR_HAND_JOINT_COUNT_EXT];
    XrHandJointVelocityEXT    leftHandVelocity [XR_HAND_JOINT_COUNT_EXT];
    XrHandJointVelocityEXT    rightHandVelocity[XR_HAND_JOINT_COUNT_EXT];

    leftHandLocations.jointLocations = leftHandLocation;
    leftHandLocations.jointCount     = XR_HAND_JOINT_COUNT_EXT;
    leftHandLocations.isActive       = XR_FALSE;
    leftHandLocations.next           = &leftHandVelocities;
    leftHandVelocities.jointVelocities = leftHandVelocity;
    leftHandVelocities.jointCount      = XR_HAND_JOINT_COUNT_EXT;
    rightHandLocations.jointLocations  = rightHandLocation;
    rightHandLocations.jointCount      = XR_HAND_JOINT_COUNT_EXT;
    rightHandLocations.isActive        = XR_FALSE;
    rightHandLocations.next            = &rightHandVelocities;
    rightHandVelocities.jointVelocities = rightHandVelocity;
    rightHandVelocities.jointCount      = XR_HAND_JOINT_COUNT_EXT;

    leftHandJointsLocateInfo.baseSpace = m_xr_reference_space;
    leftHandJointsLocateInfo.time      = m_xr_frame_state.predictedDisplayTime;

    m_instance.xrLocateHandJointsEXT(m_hand_tracker_left.hand_tracker, &leftHandJointsLocateInfo, &leftHandLocations);
    m_instance.xrLocateHandJointsEXT(m_hand_tracker_right.hand_tracker, &leftHandJointsLocateInfo/*rightHandJointsLocateInfo*/, &rightHandLocations);

    if (leftHandLocations.isActive) {
        log_xr->trace("Left hand is active");
        log_xr->trace("Left hand wrist position tracked=%s, rotation tracked=%s",
            (leftHandLocation[XR_HAND_JOINT_WRIST_EXT].locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT ? "true" : "false"),
            (leftHandLocation[XR_HAND_JOINT_WRIST_EXT].locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT ? "true" : "false"));
        log_xr->trace(
            "Left hand wrist position = %f, %f, %f",
            leftHandLocation[XR_HAND_JOINT_WRIST_EXT].pose.position.x,
            leftHandLocation[XR_HAND_JOINT_WRIST_EXT].pose.position.y,
            leftHandLocation[XR_HAND_JOINT_WRIST_EXT].pose.position.z
        );
    } else {
        log_xr->trace("Left hand *Not* tracked");
    }

#endif

    const XrHandJointsLocateInfoEXT hand_joints_locate_info{
        .type      = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
        .next      = nullptr,
        //.baseSpace = m_xr_reference_space_local,
        .baseSpace = m_xr_reference_space_stage,
        .time      = m_xr_frame_state.predictedDisplayTime
    };

    if (m_hand_tracker_left.hand_tracker != XR_NULL_HANDLE) {
        check(
            m_instance.xrLocateHandJointsEXT(
                m_hand_tracker_left.hand_tracker,
                &hand_joints_locate_info,
                &m_hand_tracker_left.locations
            )
        );
    }
    if (m_hand_tracker_right.hand_tracker != XR_NULL_HANDLE) {
        check(
            m_instance.xrLocateHandJointsEXT(
                m_hand_tracker_right.hand_tracker,
                &hand_joints_locate_info,
                &m_hand_tracker_right.locations
            )
        );
    }
}

void Xr_session::set_state(XrSessionState state)
{
    m_xr_session_state = state;
}

auto Xr_session::get_state() const -> XrSessionState
{
    return m_xr_session_state;
}

auto Xr_session::get_hand_tracking_joint(const XrHandEXT hand, const XrHandJointEXT joint) const -> Hand_tracking_joint
{
    const auto& tracker = (hand == XR_HAND_LEFT_EXT) ? m_hand_tracker_left : m_hand_tracker_right;
    return {
        tracker.joint_locations[joint],
        tracker.joint_velocities[joint]
    };
}

auto Xr_session::get_hand_tracking_active(const XrHandEXT hand) const -> bool
{
    const auto& tracker = (hand == XR_HAND_LEFT_EXT) ? m_hand_tracker_left : m_hand_tracker_right;
    return tracker.locations.isActive;
}

auto Xr_session::get_view_space_location() const -> const XrSpaceLocation&
{
    return m_view_location;
}

auto Xr_session::begin_frame() -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }

    const XrFrameBeginInfo frame_begin_info{
        .type = XR_TYPE_FRAME_BEGIN_INFO,
        .next = nullptr
    };

    {
        check_gl_context_in_current_in_this_thread();
        log_xr->trace("xrBeginFrame()");
        const auto result = xrBeginFrame(m_xr_session, &frame_begin_info);
        if (result == XR_SUCCESS) {
            return true;
        }
        if (result == XR_SESSION_LOSS_PENDING) {
            log_xr->error("TODO Handle XR_SESSION_LOSS_PENDING");
            return false;
        }
        log_xr->error("xrBeginFrame() returned error {}", c_str(result));
        return false;
    }
}

auto Xr_session::wait_frame() -> XrFrameState*
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_session == XR_NULL_HANDLE) {
        return nullptr;
    }

    if (!m_session_running) {
        return nullptr;
    }
    switch (m_xr_session_state) {
        // Proceed with xrWaitFrame in these states
        case XR_SESSION_STATE_READY:        break;
        case XR_SESSION_STATE_SYNCHRONIZED: break;
        case XR_SESSION_STATE_VISIBLE:      break;
        case XR_SESSION_STATE_FOCUSED:      break;

        // Do not call xrWaitFrame() in these states
        case XR_SESSION_STATE_UNKNOWN:      return nullptr;
        case XR_SESSION_STATE_IDLE:         return nullptr;
        case XR_SESSION_STATE_STOPPING:     return nullptr;
        case XR_SESSION_STATE_LOSS_PENDING: return nullptr;
        case XR_SESSION_STATE_EXITING:      return nullptr;
        default: return nullptr;
    }


    const XrFrameWaitInfo frame_wait_info{
        .type = XR_TYPE_FRAME_WAIT_INFO,
        .next = nullptr
    };

    m_xr_frame_state = XrFrameState{
        .type                   = XR_TYPE_FRAME_STATE,
        .next                   = nullptr,
        .predictedDisplayTime   = 0,
        .predictedDisplayPeriod = 0,
        .shouldRender           = XR_FALSE
    };
    {
        log_xr->trace("xrWaitFrame()");
        const auto result = xrWaitFrame(m_xr_session, &frame_wait_info, &m_xr_frame_state);
        if (result == XR_SUCCESS) {
            return &m_xr_frame_state;
        } else if (result == XR_SESSION_LOSS_PENDING) {
            log_xr->error("TODO Handle XR_SESSION_LOSS_PENDING");
            return nullptr;
        } else {
            log_xr->error("xrWaitFrame() returned error {}", c_str(result));
            return nullptr;
        }
    }
}

auto Xr_session::render_frame(erhe::graphics::Command_buffer& command_buffer, std::function<bool(Render_view&, erhe::graphics::Command_buffer&)> render_view_callback) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }

    uint32_t view_count_output{0};
    {
        ERHE_PROFILE_SCOPE("xrLocateViews");

        XrViewState view_state{
            .type           = XR_TYPE_VIEW_STATE,
            .next           = nullptr,
            .viewStateFlags = 0
        };
        uint32_t view_capacity_input{static_cast<uint32_t>(m_xr_views.size())};

        const XrViewLocateInfo view_locate_info{
            .type                  = XR_TYPE_VIEW_LOCATE_INFO,
            .next                  = nullptr,
            .viewConfigurationType = m_instance.get_xr_view_configuration_type(),
            .displayTime           = m_xr_frame_state.predictedDisplayTime,
            //.space                 = m_xr_reference_space_local
            .space                 = m_xr_reference_space_stage
        };

        ERHE_XR_CHECK(
            xrLocateViews(
                m_xr_session,
                &view_locate_info,
                &view_state,
                view_capacity_input,
                &view_count_output,
                m_xr_views.data()
            )
        );
        ERHE_VERIFY(view_count_output == view_capacity_input);
    }

    const auto& view_configuration_views = m_instance.get_xr_view_configuration_views();

    m_xr_composition_layer_projection_views.resize(view_count_output);
    m_xr_composition_layer_depth_infos     .resize(view_count_output);

    // Acquired-image optionals live at function scope so their destructors
    // (which call xrReleaseSwapchainImage) run AFTER cb.end + submit
    // below, not at the end of each loop iteration. Submitting before
    // release is required by XR_KHR_vulkan_enable2: the runtime tracks
    // queue submissions to know when GPU work for a swapchain image is
    // complete, so submission must precede release. On OpenGL the order
    // is harmless.
    std::vector<std::optional<Swapchain_image>> acquired_color_imgs(view_count_output);
    std::vector<std::optional<Swapchain_image>> acquired_depth_imgs(view_count_output);

    static constexpr std::string_view c_id_views{"HS views"};
    static constexpr std::string_view c_id_view{"HS view"};

    // Per-view command-buffer strategy:
    //
    // The caller-supplied `command_buffer` is the dedicated *setup* cb. It
    // already carries the editor's pre-rendergraph work (mesh_memory.flush,
    // thumbnails, ...) plus all rendergraph nodes that ran before
    // Headset_view_node (shadow_render_node, rendertarget_imgui_host, ...).
    // We allocate one fresh cb per XR view; each per-view cb only needs a
    // memory dependency on setup, never on the other views (each view writes
    // a different swapchain image and reads no view's output). So the sync
    // is a fan-out from setup -- no inter-view chain.
    //
    // Order:
    //   1. Allocate per-view cbs and register signal_semaphore on setup cb
    //      for each one. After setup_cb's submit fires, every per-view
    //      cb's implicit semaphore is signaled.
    //   2. End + submit setup_cb. The GPU starts consuming the pre-view
    //      work immediately.
    //   3. Per-view loop: wait on the swapchain image (CPU-blocking, but
    //      now overlapped with setup-cb GPU execution), begin the per-view
    //      cb, register wait_for_semaphore on its own semaphore, run the
    //      callback (which records the view's render passes into this cb),
    //      end + submit. View 0's submit kicks GPU view rendering ASAP --
    //      the CPU then records view 1 while GPU is consuming view 0, etc.
    std::vector<erhe::graphics::Command_buffer*> view_cbs(view_count_output, nullptr);
    for (uint32_t i = 0; i < view_count_output; ++i) {
        view_cbs[i] = &m_graphics_device.get_command_buffer(/*thread_slot*/ 0);
        // Make setup_cb's submit signal view_cbs[i]'s implicit semaphore.
        // Each view gets its own semaphore signal -- binary semaphores
        // can only be waited on once, so a single-signal/N-wait scheme
        // would not work.
        command_buffer.signal_semaphore(*view_cbs[i]);
    }
    command_buffer.end();
    {
        erhe::graphics::Command_buffer* cbs[] = { &command_buffer };
        m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{cbs});
    }

    for (uint32_t i = 0; i < view_count_output; ++i) {
        ERHE_PROFILE_SCOPE("view render");

        auto& swapchains = m_view_swapchains[i];
        acquired_color_imgs[i] = swapchains.color_swapchain.acquire();
        if (!acquired_color_imgs[i].has_value() || !swapchains.color_swapchain.wait()) {
            log_xr->warn("no swapchain color image for view {}", i);
            return false;
        }

        acquired_depth_imgs[i] = swapchains.depth_stencil_swapchain.acquire();
        bool use_depth_image = acquired_depth_imgs[i].has_value() && swapchains.depth_stencil_swapchain.wait();
        if (!use_depth_image) {
            log_xr->debug("no swapchain depth stencil image for view {}", i);
        }

        erhe::graphics::Texture* color_texture = acquired_color_imgs[i]->get_texture();
        if (color_texture == nullptr) {
            log_xr->warn("invalid color image for view {}", i);
            return false;
        }

        erhe::graphics::Texture* depth_stencil_texture = nullptr;
        if (use_depth_image) {
            depth_stencil_texture = acquired_depth_imgs[i]->get_texture();
            if (depth_stencil_texture == nullptr) {
                log_xr->warn("invalid depth image for view {}", i);
                use_depth_image = false;
            }
        }

        erhe::graphics::Command_buffer& view_cb = *view_cbs[i];
        view_cb.begin();
        // Wait on own semaphore (signaled by setup's submit registration
        // above). Pairing: the matching signal target was view_cb itself,
        // so the wait target is also view_cb -- "wait on the semaphore
        // setup arranged for me".
        view_cb.wait_for_semaphore(view_cb);

        Render_view render_view{
            .slot      = i,
            .view_pose = {
                .orientation = to_glm(m_xr_views[i].pose.orientation),
                .position    = to_glm(m_xr_views[i].pose.position),
            },
            .fov_left              = m_xr_views[i].fov.angleLeft,
            .fov_right             = m_xr_views[i].fov.angleRight,
            .fov_up                = m_xr_views[i].fov.angleUp,
            .fov_down              = m_xr_views[i].fov.angleDown,
            .color_texture         = color_texture,
            .depth_stencil_texture = depth_stencil_texture,
            .color_format          = m_swapchain_color_format,
            .depth_stencil_format  = m_swapchain_depth_stencil_format,
            .width                 = view_configuration_views[i].recommendedImageRectWidth,
            .height                = view_configuration_views[i].recommendedImageRectHeight,
            .composition_alpha     = m_instance.get_configuration().composition_alpha,
            .near_depth            = 0.0f, // TODO
            .far_depth             = 1.0f // TODO
        };
        {
            const auto result = render_view_callback(render_view, view_cb);
            if (result == false) {
                log_xr->warn("render callback returned false for view {}", i);
                return false;
            }
        }

        m_xr_composition_layer_depth_infos[i] = {
            .type     = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR,
            .next     = nullptr,
            .subImage = {
                .swapchain = swapchains.depth_stencil_swapchain.get_xr_swapchain(),
                .imageRect = {
                    .offset = { 0, 0 },
                    .extent = {
                        static_cast<int32_t>(view_configuration_views[i].recommendedImageRectWidth),
                        static_cast<int32_t>(view_configuration_views[i].recommendedImageRectHeight)
                    }
                },
                .imageArrayIndex = 0 // TODO
            },
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
            .nearZ    = render_view.far_depth,
            .farZ     = render_view.near_depth
        };

        m_xr_composition_layer_projection_views[i] = {
            .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
            .next = use_depth_image ? &m_xr_composition_layer_depth_infos[i] : nullptr,
            .pose = m_xr_views[i].pose,
            .fov  = m_xr_views[i].fov,
            .subImage = {
                .swapchain = swapchains.color_swapchain.get_xr_swapchain(),
                .imageRect = {
                    .offset = { 0, 0 },
                    .extent = {
                        static_cast<int32_t>(view_configuration_views[i].recommendedImageRectWidth),
                        static_cast<int32_t>(view_configuration_views[i].recommendedImageRectHeight)
                    }
                },
                .imageArrayIndex = 0 // TODO
            }
        };

        // End + submit this view's cb immediately. vkQueueSubmit2 returns
        // synchronously; the GPU starts consuming view i while the CPU
        // proceeds to record view i+1.
        view_cb.end();
        erhe::graphics::Command_buffer* cbs[] = { &view_cb };
        m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{cbs});
    }

    return true;
}

auto Xr_session::end_frame(const bool rendered) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }

    if (m_xr_composition_layer_projection_views.empty()) {
        // This can happen
        log_xr->debug("no layer views");
    }

    //XrCompositionLayerDepthTestVARJO layer_depth;
    //layer_depth.type                = XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_VARJO;
    //layer_depth.next                = nullptr;
    //layer_depth.depthTestRangeNearZ = 0.1f;
    //layer_depth.depthTestRangeFarZ  = 0.5f;

    XrCompositionLayerPassthroughFB passthrough_fb_layer{
        .type        = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB,
        .next        = nullptr,
        .flags       = 0,
        .space       = XR_NULL_HANDLE,
        .layerHandle = m_passthrough_layer_fb
    };

    XrCompositionLayerProjection projection_layer{
        .type       = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
        .next       = nullptr,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
        //.space      = m_xr_reference_space_local,
        .space      = m_xr_reference_space_stage,
        .viewCount  = static_cast<uint32_t>(m_xr_views.size()),
        .views      = m_xr_composition_layer_projection_views.data()
    };

    std::vector<XrCompositionLayerBaseHeader*> layers;
    if (rendered) {
        if (m_instance.extensions.FB_passthrough) {
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&passthrough_fb_layer));
        }
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection_layer));
    }

    XrFrameEndInfo frame_end_info{
        .type                 = XR_TYPE_FRAME_END_INFO,
        .next                 = nullptr,
        .displayTime          = m_xr_frame_state.predictedDisplayTime,
        .environmentBlendMode = m_instance.get_xr_environment_blend_mode(),
        .layerCount           = static_cast<uint32_t>(layers.size()),
        .layers               = layers.empty() ? nullptr : layers.data()
    };

    if (rendered) {
        check_gl_context_in_current_in_this_thread();
    }
    log_xr->trace("xrEndFrame");
    const XrResult result = xrEndFrame(m_xr_session, &frame_end_info);
    ERHE_XR_CHECK(result);

    return true;
}

} // namespace erhe::xr
