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
#include "erhe_xr/xr_quad_layer.hpp"
#include "erhe_xr/xr_swapchain_image.hpp"

#include <fmt/format.h>

#include <algorithm>

#if defined(ERHE_OS_WINDOWS)
#   include <unknwn.h>
#endif

// volk.h must precede <openxr/openxr_platform.h> when XR_USE_GRAPHICS_API_VULKAN
// is defined: openxr_platform.h references VkFormat / VkImage / etc. without
// including <vulkan/vulkan.h> itself.
#if defined(XR_USE_GRAPHICS_API_VULKAN)
# include "volk.h"
#endif

// jni.h must precede openxr_platform.h on Android: the platform header
// references jobject (XrInstanceCreateInfoAndroidKHR, XrLoaderInitInfoAndroidKHR).
#if defined(XR_USE_PLATFORM_ANDROID)
# include <jni.h>
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
    enable_performance_metrics();
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

    // Destroy every swapchain while the owning XrSession handle is still
    // valid. xrDestroySession invalidates all child handles (swapchains,
    // spaces), so any Swapchain whose ~Swapchain runs after the
    // xrDestroySession() below would call xrDestroySwapchain() on an
    // already-invalid handle and log XR_ERROR_HANDLE_INVALID. The per-eye
    // m_view_swapchains were always cleared here; the multiview shared
    // swapchains are std::optional members whose destructors would
    // otherwise run during member teardown (after this body), so reset
    // them explicitly too.
    m_view_swapchains.clear();
    m_shared_color_swapchain.reset();
    m_shared_depth_stencil_swapchain.reset();

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

auto Xr_session::get_swapchain_color_format() const -> erhe::dataformat::Format
{
    return m_swapchain_color_format;
}

auto Xr_session::get_swapchain_depth_stencil_format() const -> erhe::dataformat::Format
{
    return m_swapchain_depth_stencil_format;
}

auto Xr_session::get_swapchain_sample_count() const -> uint32_t
{
    const auto& views = m_instance.get_xr_view_configuration_views();
    if (views.empty()) {
        return 1u;
    }
    return views[0].recommendedSwapchainSampleCount;
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
    // Stencil-bearing formats are preferred so the editor's selection-outline
    // stencil scheme (Pipeline_renderpasses::polygon_fill_standard_selected
    // writes bit 7 of stencil; Pipeline_renderpasses::outline tests bit 7)
    // works on the headset. The actual swapchain usage is DS+SAMPLED only
    // (XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT in create_swapchains;
    // depth_stencil_texture_usage = DS|SAMPLED). enumerate_swapchain_formats
    // probes each candidate against that mask and falls back if the local
    // Vulkan device cannot honour a stencil-bearing format with those
    // flags. Previously d24/d32_s8 were hard-zeroed citing the full
    // DS+SAMPLED+TSRC+TDST+STORAGE mask, but that mask is never requested
    // for the XR depth swapchain.
    switch (pixelformat) {
        //using enum gl::Internal_format;
        case erhe::dataformat::Format::format_s8_uint:             return 0;
        case erhe::dataformat::Format::format_d16_unorm:           return 1;
        case erhe::dataformat::Format::format_x8_d24_unorm_pack32: return 0; // runtime requirements not met
        case erhe::dataformat::Format::format_d32_sfloat:          return 3;
        case erhe::dataformat::Format::format_d24_unorm_s8_uint:   return 4;
        case erhe::dataformat::Format::format_d32_sfloat_s8_uint:  return 5;
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
    std::vector<std::pair<int, erhe::dataformat::Format>> depth_candidates;
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
        if (depth_stencil_score > 0) {
            depth_candidates.push_back(std::pair<int, erhe::dataformat::Format>{depth_stencil_score, pixelformat});
        }
    }

    // Probe each depth candidate against the actual XR depth swapchain
    // usage (DS+SAMPLED) and pick the highest-scoring format the device
    // says it can create with that usage. This gates out e.g. d24_s8 on
    // a Vulkan device that does not advertise sampled-image support for
    // it, while preferring stencil-bearing formats so the selection
    // outline stencil scheme works on the headset. The full mask
    // (DS+SAMPLED+TSRC+TDST+STORAGE) is logged for diagnostic context
    // only; on GL probe_image_format_support returns true unconditionally
    // so the highest-scored candidate always wins. See
    // doc/vulkan_backend.md log.
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
        int best_depth_stencil_score{0};
        for (const std::pair<int, erhe::dataformat::Format>& candidate_entry : depth_candidates) {
            const int                      score     = candidate_entry.first;
            const erhe::dataformat::Format candidate = candidate_entry.second;
            const bool full_ok = m_graphics_device.probe_image_format_support(candidate, full_usage_mask);
            const bool min_ok  = m_graphics_device.probe_image_format_support(candidate, min_usage_mask);
            if (full_ok) { ++full_pass_count; }
            if (min_ok)  { ++min_pass_count; }
            log_xr->info(
                "    {} -> full={} min={} score={}",
                erhe::dataformat::c_str(candidate),
                full_ok ? "PASS" : "FAIL",
                min_ok  ? "PASS" : "FAIL",
                score
            );
            if (min_ok && (score > best_depth_stencil_score)) {
                best_depth_stencil_score = score;
                m_swapchain_depth_stencil_format = candidate;
            }
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
    m_shared_color_swapchain        .reset();
    m_shared_depth_stencil_swapchain.reset();

    const int64_t native_color_format         = erhe::graphics::dataformat_to_native_swapchain_format(m_swapchain_color_format);
    const int64_t native_depth_stencil_format = erhe::graphics::dataformat_to_native_swapchain_format(m_swapchain_depth_stencil_format);
    ERHE_VERIFY(native_color_format != 0);

    constexpr uint64_t color_texture_usage =
        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
        erhe::graphics::Image_usage_flag_bit_mask::sampled;
    constexpr uint64_t depth_stencil_texture_usage =
        erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
        erhe::graphics::Image_usage_flag_bit_mask::sampled;

    // Fixed foveated rendering (issue #208): request a fragment-density-map on the
    // COLOR swapchains when the config knob, the OpenXR foveation extensions, and
    // the Vulkan device's FDM capability all line up. Vulkan-only; on other
    // backends use_foveation stays false and swapchains are created exactly as
    // before. The FDM is requested only on color (never depth). chaining the
    // create-info is harmless if the runtime ignores it, but it is gated anyway.
    bool use_foveation = false;
#if defined(XR_USE_GRAPHICS_API_VULKAN)
    use_foveation = m_instance.extensions.FB_foveation && m_graphics_device.get_info().fragment_density_map;
#endif
    const XrSwapchainCreateInfoFoveationFB color_foveation_create_info{
        .type  = XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB,
        .next  = nullptr,
        .flags = XR_SWAPCHAIN_CREATE_FOVEATION_FRAGMENT_DENSITY_MAP_BIT_FB
    };
    const void* const color_swapchain_next = use_foveation ? &color_foveation_create_info : nullptr;
    log_xr->info("OpenXR fixed foveated rendering: use_foveation={}", use_foveation);

    // Create and apply the FFR foveation profile to a color swapchain. Shared by
    // the multiview and per-eye paths. Every failure is non-fatal: a color
    // swapchain with an FDM attachment but no (or a failed) profile simply renders
    // at full density (no foveation), which is safe. No-op unless FFR is active and
    // the entry points were loaded (so it is inert on non-Vulkan / unsupported
    // runtimes).
    const auto apply_foveation_profile = [&](XrSwapchain color_xr_swapchain) {
        if (!use_foveation ||
            (m_instance.xrCreateFoveationProfileFB == nullptr) ||
            (m_instance.xrUpdateSwapchainFB        == nullptr)) {
            return;
        }
        const Headset_config& config = m_instance.get_configuration();
        // Non-const: XrFoveationProfileCreateInfoFB::next is void* (not const void*),
        // so it cannot point at a const object.
        XrFoveationLevelProfileCreateInfoFB level_profile{
            .type           = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB,
            .next           = nullptr,
            // Foveation_level maps 1:1 to XrFoveationLevelFB (none/low/medium/high).
            .level          = static_cast<XrFoveationLevelFB>(static_cast<int>(config.foveation_level)),
            .verticalOffset = 0.0f,
            .dynamic        = config.foveation_dynamic ? XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB : XR_FOVEATION_DYNAMIC_DISABLED_FB
        };
        const XrFoveationProfileCreateInfoFB profile_create_info{
            .type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB,
            .next = &level_profile
        };
        XrFoveationProfileFB foveation_profile{XR_NULL_HANDLE};
        const XrResult create_result = m_instance.xrCreateFoveationProfileFB(m_xr_session, &profile_create_info, &foveation_profile);
        if (create_result != XR_SUCCESS) {
            log_xr->warn("xrCreateFoveationProfileFB() failed with error {}; foveation inactive for this swapchain", c_str(create_result));
            return;
        }
        const XrSwapchainStateFoveationFB foveation_state{
            .type    = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB,
            .next    = nullptr,
            .flags   = 0,
            .profile = foveation_profile
        };
        const XrResult update_result = m_instance.xrUpdateSwapchainFB(color_xr_swapchain, reinterpret_cast<const XrSwapchainStateBaseHeaderFB*>(&foveation_state));
        if (update_result != XR_SUCCESS) {
            log_xr->warn("xrUpdateSwapchainFB() failed with error {}; foveation inactive for this swapchain", c_str(update_result));
        } else {
            log_xr->info("OpenXR foveation profile applied: level={} dynamic={}", static_cast<int>(config.foveation_level), config.foveation_dynamic);
        }
        if (m_instance.xrDestroyFoveationProfileFB != nullptr) {
            m_instance.xrDestroyFoveationProfileFB(foveation_profile);
        }
    };

    // On Vulkan the depth swapchain creation is currently disabled (see
    // doc/vulkan_backend.md). On GL the runtime accepts the depth format
    // and the editor uses the resulting depth texture for the projection
    // layer's depth info.
    bool create_depth_stencil_swapchain = m_instance.get_configuration().swapchain_depth_attachment;

    const auto& views = m_instance.get_xr_view_configuration_views();

    // Multiview swapchain path: a single shared color + (optional) depth
    // XrSwapchain with arraySize = view_count, wrapped as 2D array
    // textures so VK_KHR_multiview can render every layer in one pass.
    // Gated on:
    //   - Vulkan multiview device feature (queried at device init)
    //   - exactly two views (primary stereo); other view counts are
    //     valid OpenXR configurations but the engine has no multiview
    //     plumbing for them yet.
    //   - all per-view extents identical (multiview demands a single
    //     image of one width x height; non-uniform per-view extents
    //     mean we must keep the per-eye-swapchain path).
    m_use_multiview = false;
#if defined(XR_USE_GRAPHICS_API_VULKAN)
    if ((views.size() == 2) && m_graphics_device.get_info().multiview) {
        const auto& view0 = views[0];
        const auto& view1 = views[1];
        const bool extents_match =
            (view0.recommendedImageRectWidth      == view1.recommendedImageRectWidth) &&
            (view0.recommendedImageRectHeight     == view1.recommendedImageRectHeight) &&
            (view0.recommendedSwapchainSampleCount == view1.recommendedSwapchainSampleCount);
        if (extents_match) {
            m_use_multiview = true;
        } else {
            log_xr->info("Multiview disabled: per-view recommended extents differ across eyes");
        }
    }
#endif

    if (m_use_multiview) {
        const auto& view = views[0];
        const uint32_t view_count = static_cast<uint32_t>(views.size());

        const XrSwapchainCreateInfo color_swapchain_create_info{
            .type        = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .next        = color_swapchain_next,
            .createFlags = 0,
            .usageFlags  = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
            .format      = native_color_format,
            .sampleCount = view.recommendedSwapchainSampleCount,
            .width       = view.recommendedImageRectWidth,
            .height      = view.recommendedImageRectHeight,
            .faceCount   = 1,
            .arraySize   = view_count,
            .mipCount    = 1
        };
        XrSwapchain color_xr_swapchain{XR_NULL_HANDLE};
        check_gl_context_in_current_in_this_thread();
        const XrResult color_result = xrCreateSwapchain(m_xr_session, &color_swapchain_create_info, &color_xr_swapchain);
        if (color_result != XR_SUCCESS) {
            log_xr->error("xrCreateSwapchain() (multiview color) failed with error {}", c_str(color_result));
            return false;
        }

        XrSwapchain depth_stencil_xr_swapchain{XR_NULL_HANDLE};
        if (create_depth_stencil_swapchain && (native_depth_stencil_format != 0)) {
            // Submit the depth swapchain as a plain depth-stencil attachment, the
            // OpenXR-standard usage for XR_KHR_composition_layer_depth: the runtime
            // owns the underlying image and reads it for the depth-based composition
            // / XR_FB_composition_layer_depth_test itself, so the app does not request
            // storage or sampled usage. (An earlier attempt OR-ed in
            // XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT on the theory that the compositor
            // reads depth as a storage image; that did not enable occlusion and is not
            // what Meta's depth-submission docs describe. Removed while diagnosing why
            // the HUD quad is not occluded by the submitted scene depth even though the
            // depth swapchain and ENABLE_DEPTH_TEST flag both reach the compositor. See
            // doc/hud_hotbar_depth_test_plan.md.)
            const uint64_t depth_usage_flags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            const XrSwapchainCreateInfo depth_stencil_swapchain_create_info{
                .type        = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                .next        = nullptr,
                .createFlags = 0,
                .usageFlags  = depth_usage_flags,
                .format      = native_depth_stencil_format,
                .sampleCount = view.recommendedSwapchainSampleCount,
                .width       = view.recommendedImageRectWidth,
                .height      = view.recommendedImageRectHeight,
                .faceCount   = 1,
                .arraySize   = view_count,
                .mipCount    = 1
            };
            check_gl_context_in_current_in_this_thread();
            const XrResult depth_stencil_result = xrCreateSwapchain(m_xr_session, &depth_stencil_swapchain_create_info, &depth_stencil_xr_swapchain);
            if (depth_stencil_result != XR_SUCCESS) {
                // Non-fatal: some runtimes/drivers (e.g. Vulkan over Meta Link)
                // cannot provide the depth swapchain format/usage the runtime
                // requires. Continue without composition-layer depth -- the quad
                // depth test then self-disables (condition 1, see
                // doc/hud_hotbar_depth_test_plan.md). Do NOT abort the session or
                // destroy the color swapchain.
                log_xr->warn("xrCreateSwapchain() (multiview depth) failed with error {}; continuing without composition-layer depth", c_str(depth_stencil_result));
                depth_stencil_xr_swapchain = XR_NULL_HANDLE;
            }
        }

        m_shared_color_swapchain.emplace(
            m_graphics_device,
            color_xr_swapchain,
            m_swapchain_color_format,
            view.recommendedImageRectWidth,
            view.recommendedImageRectHeight,
            view.recommendedSwapchainSampleCount,
            view_count,
            color_texture_usage,
            use_foveation,
            "XR multiview color"
        );
        apply_foveation_profile(color_xr_swapchain);
        if (depth_stencil_xr_swapchain != XR_NULL_HANDLE) {
            m_shared_depth_stencil_swapchain.emplace(
                m_graphics_device,
                depth_stencil_xr_swapchain,
                m_swapchain_depth_stencil_format,
                view.recommendedImageRectWidth,
                view.recommendedImageRectHeight,
                view.recommendedSwapchainSampleCount,
                view_count,
                depth_stencil_texture_usage,
                /*want_fragment_density_map*/ false,
                "XR multiview depth stencil"
            );
        }
        log_xr->info("OpenXR multiview swapchain created: {}x{}, {} views, sampleCount {}",
            view.recommendedImageRectWidth, view.recommendedImageRectHeight,
            view_count, view.recommendedSwapchainSampleCount);
        // Depth swapchain usability (condition 1 of the quad depth-test gate, see
        // doc/hud_hotbar_depth_test_plan.md). On Quest this should be usable; on
        // Vulkan/Meta-Link the runtime's required depth format/usage is typically
        // unavailable, so it is not created.
        log_xr->info("OpenXR multiview depth swapchain: requested={} native_depth_format={} usable={}",
            create_depth_stencil_swapchain, native_depth_stencil_format,
            m_shared_depth_stencil_swapchain.has_value());
        // Convention of the depth we submit to the compositor (drives the
        // XrCompositionLayerDepthInfoKHR nearZ/farZ mapping and how the FB quad
        // depth test interprets the scene depth). Logged so the on-device value is
        // unambiguous even after a config migration.
        log_xr->info("OpenXR submitted projection depth: reverse_depth={}", m_graphics_device.get_reverse_depth());

        // Skip the per-eye swapchain creation loop below.
        if (m_instance.extensions.FB_passthrough) {
            // Fall through to the passthrough setup at the end of this
            // function by jumping past the per-eye creation loop.
        }
        // The original passthrough block lives below the per-eye loop;
        // returning here would skip it. Instead, replicate by leaving
        // the per-eye loop disabled via an early branch and continuing
        // execution at the function tail. The per-eye creation loop is
        // wrapped in `if (!m_use_multiview) { ... }` below.
    }

    if (!m_use_multiview)
    {
    for (uint32_t view_index = 0; view_index < views.size(); ++view_index) {
        const auto& view = views[view_index];
        const XrSwapchainCreateInfo color_swapchain_create_info{
            .type        = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .next        = color_swapchain_next,
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
                /*array_layer_count*/ 1,
                color_texture_usage,
                use_foveation,
                fmt::format("XR color view {}", view_index)
            },
            Swapchain{
                m_graphics_device,
                depth_stencil_xr_swapchain,
                m_swapchain_depth_stencil_format,
                view.recommendedImageRectWidth,
                view.recommendedImageRectHeight,
                view.recommendedSwapchainSampleCount,
                /*array_layer_count*/ 1,
                depth_stencil_texture_usage,
                /*want_fragment_density_map*/ false,
                fmt::format("XR depth stencil view {}", view_index)
            }
        );
        apply_foveation_profile(color_xr_swapchain);
    }
    } // if (!m_use_multiview)

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
            // Both the passthrough and its layer are created with
            // XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB, so no explicit
            // xrPassthroughStartFB / xrPassthroughLayerResumeFB is needed.
            // The Quest runtime in fact rejects the redundant start with
            // XR_ERROR_UNEXPECTED_STATE_PASSTHROUGH_FB.
            m_passthrough_running = true;
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

void Xr_session::enable_performance_metrics()
{
    if (!m_instance.extensions.META_performance_metrics) {
        return;
    }
    if (m_xr_session == XR_NULL_HANDLE) {
        return;
    }
    if (m_instance.xrSetPerformanceMetricsStateMETA == nullptr) {
        return;
    }
    if (m_instance.xrEnumeratePerformanceMetricsCounterPathsMETA == nullptr) {
        return;
    }
    if (m_instance.xrQueryPerformanceMetricsCounterMETA == nullptr) {
        return;
    }

    XrPerformanceMetricsStateMETA state{
        .type    = XR_TYPE_PERFORMANCE_METRICS_STATE_META,
        .next    = nullptr,
        .enabled = XR_TRUE
    };
    XrResult result = m_instance.xrSetPerformanceMetricsStateMETA(m_xr_session, &state);
    if (result != XR_SUCCESS) {
        log_xr->warn("xrSetPerformanceMetricsStateMETA() failed with {}", c_str(result));
        return;
    }

    uint32_t path_count_output = 0;
    result = m_instance.xrEnumeratePerformanceMetricsCounterPathsMETA(
        m_instance.get_xr_instance(), 0, &path_count_output, nullptr
    );
    if (result != XR_SUCCESS) {
        log_xr->warn("xrEnumeratePerformanceMetricsCounterPathsMETA(size) failed with {}", c_str(result));
        return;
    }
    if (path_count_output == 0) {
        log_xr->info("XR_META_performance_metrics: runtime exposes 0 counters");
        m_perf_metrics_enabled = true;
        return;
    }

    std::vector<XrPath> paths(path_count_output);
    uint32_t filled = 0;
    result = m_instance.xrEnumeratePerformanceMetricsCounterPathsMETA(
        m_instance.get_xr_instance(), path_count_output, &filled, paths.data()
    );
    if (result != XR_SUCCESS) {
        log_xr->warn("xrEnumeratePerformanceMetricsCounterPathsMETA(fill) failed with {}", c_str(result));
        return;
    }

    m_perf_counters.clear();
    m_perf_counters.reserve(filled);
    for (uint32_t i = 0; i < filled; ++i) {
        const std::string full = m_instance.get_path_string(paths[i]);
        // Strip the "/perfmetrics_meta/" prefix if present so the plot label
        // is short and human-readable.
        std::string display = full;
        const std::string_view prefix{"/perfmetrics_meta/"};
        if (display.size() > prefix.size() &&
            std::string_view{display}.substr(0, prefix.size()) == prefix)
        {
            display.erase(0, prefix.size());
        }
        Xr_perf_counter counter{};
        counter.path = paths[i];
        counter.display_name = std::move(display);
        counter.last.type = XR_TYPE_PERFORMANCE_METRICS_COUNTER_META;
        counter.last.next = nullptr;
        m_perf_counters.push_back(std::move(counter));
        log_xr->info("XR_META_performance_metrics counter: {}", full);
    }

    m_perf_metrics_enabled = true;
}

void Xr_session::query_performance_metrics()
{
    if (!m_perf_metrics_enabled) {
        return;
    }
    if (m_instance.xrQueryPerformanceMetricsCounterMETA == nullptr) {
        return;
    }
    if (m_xr_session == XR_NULL_HANDLE) {
        return;
    }

    for (Xr_perf_counter& counter : m_perf_counters) {
        counter.last.type = XR_TYPE_PERFORMANCE_METRICS_COUNTER_META;
        counter.last.next = nullptr;
        const XrResult result = m_instance.xrQueryPerformanceMetricsCounterMETA(
            m_xr_session, counter.path, &counter.last
        );
        if (result != XR_SUCCESS) {
            // Counter unavailable this frame: leave the cached value as-is
            // and continue. Don't spam the log; many counters are
            // intermittently unsupported on different runtimes.
        }
    }
}

auto Xr_session::get_perf_counters() const -> const std::vector<Xr_perf_counter>&
{
    return m_perf_counters;
}

void Xr_session::set_passthrough_active(const bool active)
{
    if ((m_passthrough_fb == XR_NULL_HANDLE) || (m_passthrough_layer_fb == XR_NULL_HANDLE)) {
        return; // Passthrough (or its layer) was never successfully created.
    }
    if (active == m_passthrough_running) {
        return;
    }
    if (active) {
        const XrResult result = m_instance.xrPassthroughStartFB(m_passthrough_fb);
        if (result != XR_SUCCESS) {
            log_xr->warn("xrPassthroughStartFB() failed with error {}", c_str(result));
            return;
        }
        m_passthrough_running = true;
        log_xr->info("XR_FB_passthrough resumed");
    } else {
        const XrResult result = m_instance.xrPassthroughPauseFB(m_passthrough_fb);
        if (result != XR_SUCCESS) {
            log_xr->warn("xrPassthroughPauseFB() failed with error {}", c_str(result));
            return;
        }
        m_passthrough_running = false;
        log_xr->info("XR_FB_passthrough paused");
        // The runtime restores the boundary on its own once passthrough stops
        // being rendered; request it explicitly (always allowed per spec) so
        // the state change is deterministic and visible in the log.
        request_boundary_visibility(XR_BOUNDARY_VISIBILITY_NOT_SUPPRESSED_META);
    }
}

auto Xr_session::is_passthrough_active() const -> bool
{
    return m_passthrough_running;
}

void Xr_session::set_boundary_visibility(const XrBoundaryVisibilityMETA boundary_visibility)
{
    m_boundary_visibility = boundary_visibility;
}

void Xr_session::request_boundary_visibility(const XrBoundaryVisibilityMETA boundary_visibility)
{
    if (!m_instance.extensions.META_boundary_visibility || (m_instance.xrRequestBoundaryVisibilityMETA == nullptr)) {
        return;
    }
    const XrResult result = m_instance.xrRequestBoundaryVisibilityMETA(m_xr_session, boundary_visibility);
    if ((result != m_last_boundary_visibility_request_result) || (boundary_visibility != m_last_boundary_visibility_request)) {
        // XR_BOUNDARY_VISIBILITY_SUPPRESSION_NOT_ALLOWED_META is a qualified
        // success the runtime returns until the compositor has actually seen a
        // passthrough layer; end_frame() keeps retrying, so log it as info and
        // only on changes (of either the requested visibility or the result)
        // to avoid per-frame spam.
        const char* const visibility_name =
            (boundary_visibility == XR_BOUNDARY_VISIBILITY_SUPPRESSED_META) ? "suppressed" : "not suppressed";
        if (XR_SUCCEEDED(result)) {
            log_xr->info("xrRequestBoundaryVisibilityMETA({}) returned {}", visibility_name, c_str(result));
        } else {
            log_xr->warn("xrRequestBoundaryVisibilityMETA({}) failed with error {}", visibility_name, c_str(result));
        }
        m_last_boundary_visibility_request_result = result;
        m_last_boundary_visibility_request        = boundary_visibility;
    }
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
    //   1. Allocate per-view cbs and register signal_gpu on setup cb
    //      for each one. After setup_cb's submit fires, every per-view
    //      cb's implicit GPU primitive is signaled.
    //   2. End + submit setup_cb. The GPU starts consuming the pre-view
    //      work immediately.
    //   3. Per-view loop: wait on the swapchain image (CPU-blocking, but
    //      now overlapped with setup-cb GPU execution), begin the per-view
    //      cb, register wait_for_gpu on its own GPU primitive, run the
    //      callback (which records the view's render passes into this cb),
    //      end + submit. View 0's submit kicks GPU view rendering ASAP --
    //      the CPU then records view 1 while GPU is consuming view 0, etc.
    std::vector<erhe::graphics::Command_buffer*> view_cbs(view_count_output, nullptr);
    for (uint32_t i = 0; i < view_count_output; ++i) {
        view_cbs[i] = &m_graphics_device.get_command_buffer(/*thread_slot*/ 0);
        // Make setup_cb's submit signal view_cbs[i]'s implicit GPU primitive.
        // Each view gets its own signal -- binary semaphores can only be
        // waited on once, so a single-signal/N-wait scheme would not work.
        command_buffer.signal_gpu(*view_cbs[i]);
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
        // Wait on own GPU primitive (signaled by setup's submit registration
        // above). Pairing: the matching signal target was view_cb itself,
        // so the wait target is also view_cb -- "wait on the GPU primitive
        // setup arranged for me".
        view_cb.wait_for_gpu(view_cb);

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
            .fragment_density_map_texture = acquired_color_imgs[i]->get_fragment_density_map_texture(),
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

        // nearZ/farZ are the world-space distances that minDepth/maxDepth map to.
        // Reverse-Z maps minDepth(0) to the far plane and maxDepth(1) to the near
        // plane; forward-Z is the opposite. nearZ > farZ is also how the spec flags
        // a reversed-Z buffer to the runtime, so this must follow the device's
        // actual depth convention or the compositor mis-linearizes our depth.
        const bool reverse_depth = m_graphics_device.get_reverse_depth();
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
            .nearZ    = reverse_depth ? render_view.far_depth  : render_view.near_depth,
            .farZ     = reverse_depth ? render_view.near_depth : render_view.far_depth
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

auto Xr_session::is_multiview_enabled() const -> bool
{
    return m_use_multiview;
}

auto Xr_session::get_view_count() const -> uint32_t
{
    return static_cast<uint32_t>(m_instance.get_xr_view_configuration_views().size());
}

auto Xr_session::render_frame_multiview(
    erhe::graphics::Command_buffer& command_buffer,
    std::function<bool(const Render_views_frame&, erhe::graphics::Command_buffer&)> render_views_callback
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_session == XR_NULL_HANDLE) {
        return false;
    }
    if (!m_use_multiview || !m_shared_color_swapchain.has_value()) {
        log_xr->error("render_frame_multiview() called when multiview is not enabled");
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
        const uint32_t view_capacity_input{static_cast<uint32_t>(m_xr_views.size())};

        const XrViewLocateInfo view_locate_info{
            .type                  = XR_TYPE_VIEW_LOCATE_INFO,
            .next                  = nullptr,
            .viewConfigurationType = m_instance.get_xr_view_configuration_type(),
            .displayTime           = m_xr_frame_state.predictedDisplayTime,
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
    const uint32_t view_count = view_count_output;

    m_xr_composition_layer_projection_views.resize(view_count);
    m_xr_composition_layer_depth_infos     .resize(view_count);

    // Acquire the single shared color and depth swapchain images.
    // Acquired-image optionals live at function scope so their destructors
    // run after submit (matches the per-view path's reasoning).
    std::optional<Swapchain_image> acquired_color = m_shared_color_swapchain->acquire();
    if (!acquired_color.has_value() || !m_shared_color_swapchain->wait()) {
        log_xr->warn("multiview: no shared color swapchain image");
        return false;
    }
    std::optional<Swapchain_image> acquired_depth;
    bool use_depth_image = false;
    if (m_shared_depth_stencil_swapchain.has_value()) {
        acquired_depth = m_shared_depth_stencil_swapchain->acquire();
        use_depth_image = acquired_depth.has_value() && m_shared_depth_stencil_swapchain->wait();
        if (!use_depth_image) {
            log_xr->debug("multiview: no shared depth swapchain image");
        }
    }
    // Projection depth is submitted to the compositor only when a depth image was
    // acquired AND KHR_composition_layer_depth is enabled. The FB quad depth test
    // (end_frame) is gated on this, and chaining the KHR depth info below is gated
    // on it too so we never chain a KHR struct when the extension is disabled.
    m_projection_depth_submitted = use_depth_image && m_instance.extensions.KHR_composition_layer_depth;

    erhe::graphics::Texture* color_texture = acquired_color->get_texture();
    if (color_texture == nullptr) {
        log_xr->warn("multiview: invalid color image");
        return false;
    }
    erhe::graphics::Texture* depth_stencil_texture = nullptr;
    if (use_depth_image) {
        depth_stencil_texture = acquired_depth->get_texture();
        if (depth_stencil_texture == nullptr) {
            log_xr->warn("multiview: invalid depth image");
            use_depth_image = false;
        }
    }

    // Single per-frame view command buffer: setup_cb signals views_cb's
    // implicit GPU primitive; views_cb wait_for_gpu's it. Same fan-out
    // pattern as render_frame() but with one fanout instead of N.
    erhe::graphics::Command_buffer& views_cb = m_graphics_device.get_command_buffer(/*thread_slot*/ 0);
    command_buffer.signal_gpu(views_cb);
    command_buffer.end();
    {
        erhe::graphics::Command_buffer* cbs[] = { &command_buffer };
        m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{cbs});
    }

    views_cb.begin();
    views_cb.wait_for_gpu(views_cb);

    Render_views_frame frame;
    frame.shared_color_texture         = color_texture;
    frame.shared_depth_stencil_texture = depth_stencil_texture;
    frame.shared_fragment_density_map_texture = acquired_color->get_fragment_density_map_texture();
    frame.width                        = view_configuration_views[0].recommendedImageRectWidth;
    frame.height                       = view_configuration_views[0].recommendedImageRectHeight;
    frame.view_mask                    = (view_count >= 32) ? 0xFFFFFFFFu : ((1u << view_count) - 1u);
    frame.views.reserve(view_count);
    for (uint32_t i = 0; i < view_count; ++i) {
        frame.views.push_back(Render_view{
            .slot      = i,
            .view_pose = {
                .orientation = to_glm(m_xr_views[i].pose.orientation),
                .position    = to_glm(m_xr_views[i].pose.position),
            },
            .fov_left              = m_xr_views[i].fov.angleLeft,
            .fov_right             = m_xr_views[i].fov.angleRight,
            .fov_up                = m_xr_views[i].fov.angleUp,
            .fov_down              = m_xr_views[i].fov.angleDown,
            // The shared 2D-array textures are exposed via the frame's
            // shared_*_texture pointers; the per-view entries leave
            // these null to make it explicit that the per-eye-image
            // pattern does not apply to the multiview path.
            .color_texture         = nullptr,
            .depth_stencil_texture = nullptr,
            .color_format          = m_swapchain_color_format,
            .depth_stencil_format  = m_swapchain_depth_stencil_format,
            .width                 = view_configuration_views[i].recommendedImageRectWidth,
            .height                = view_configuration_views[i].recommendedImageRectHeight,
            .composition_alpha     = m_instance.get_configuration().composition_alpha,
            .near_depth            = 0.0f, // TODO
            .far_depth             = 1.0f  // TODO
        });
    }

    {
        const bool result = render_views_callback(frame, views_cb);
        if (result == false) {
            log_xr->warn("multiview render callback returned false");
            return false;
        }
    }

    // Build composition layer projection views; both views point to the
    // same shared swapchain with imageArrayIndex selecting the per-eye
    // layer.
    // nearZ/farZ are the world-space distances that minDepth/maxDepth map to.
    // Reverse-Z maps minDepth(0) to the far plane and maxDepth(1) to the near
    // plane; forward-Z is the opposite. nearZ > farZ is also how the spec flags a
    // reversed-Z buffer to the runtime, so this must follow the device's actual
    // depth convention or the compositor mis-linearizes our depth.
    const bool reverse_depth = m_graphics_device.get_reverse_depth();
    for (uint32_t i = 0; i < view_count; ++i) {
        m_xr_composition_layer_depth_infos[i] = {
            .type     = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR,
            .next     = nullptr,
            .subImage = {
                .swapchain = (use_depth_image && m_shared_depth_stencil_swapchain.has_value())
                    ? m_shared_depth_stencil_swapchain->get_xr_swapchain()
                    : XR_NULL_HANDLE,
                .imageRect = {
                    .offset = { 0, 0 },
                    .extent = {
                        static_cast<int32_t>(view_configuration_views[i].recommendedImageRectWidth),
                        static_cast<int32_t>(view_configuration_views[i].recommendedImageRectHeight)
                    }
                },
                .imageArrayIndex = i
            },
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
            .nearZ    = reverse_depth ? frame.views[i].far_depth  : frame.views[i].near_depth,
            .farZ     = reverse_depth ? frame.views[i].near_depth : frame.views[i].far_depth
        };

        m_xr_composition_layer_projection_views[i] = {
            .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
            .next = m_projection_depth_submitted ? &m_xr_composition_layer_depth_infos[i] : nullptr,
            .pose = m_xr_views[i].pose,
            .fov  = m_xr_views[i].fov,
            .subImage = {
                .swapchain = m_shared_color_swapchain->get_xr_swapchain(),
                .imageRect = {
                    .offset = { 0, 0 },
                    .extent = {
                        static_cast<int32_t>(view_configuration_views[i].recommendedImageRectWidth),
                        static_cast<int32_t>(view_configuration_views[i].recommendedImageRectHeight)
                    }
                },
                .imageArrayIndex = i
            }
        };
    }

    views_cb.end();
    {
        erhe::graphics::Command_buffer* cbs[] = { &views_cb };
        m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{cbs});
    }

    return true;
}

auto Xr_session::create_quad_layer(uint32_t width, uint32_t height, const std::string& debug_label) -> std::unique_ptr<Quad_layer>
{
    if (m_xr_session == XR_NULL_HANDLE) {
        return nullptr;
    }

    const int64_t native_color_format = erhe::graphics::dataformat_to_native_swapchain_format(m_swapchain_color_format);
    if (native_color_format == 0) {
        log_xr->warn("create_quad_layer(): no native color swapchain format");
        return nullptr;
    }

    const XrSwapchainCreateInfo color_swapchain_create_info{
        .type        = XR_TYPE_SWAPCHAIN_CREATE_INFO,
        .next        = nullptr,
        .createFlags = 0,
        .usageFlags  = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
        .format      = native_color_format,
        .sampleCount = 1,
        .width       = width,
        .height      = height,
        .faceCount   = 1,
        .arraySize   = 1,
        .mipCount    = 1
    };

    XrSwapchain color_xr_swapchain{XR_NULL_HANDLE};
    check_gl_context_in_current_in_this_thread();
    const XrResult color_result = xrCreateSwapchain(m_xr_session, &color_swapchain_create_info, &color_xr_swapchain);
    if (color_result != XR_SUCCESS) {
        log_xr->error("create_quad_layer(): xrCreateSwapchain() failed with error {}", c_str(color_result));
        return nullptr;
    }

    constexpr uint64_t color_texture_usage =
        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
        erhe::graphics::Image_usage_flag_bit_mask::sampled;

    Swapchain swapchain{
        m_graphics_device,
        color_xr_swapchain,
        m_swapchain_color_format,
        width,
        height,
        /*sample_count*/      1,
        /*array_layer_count*/ 1,
        color_texture_usage,
        /*want_fragment_density_map*/ false,
        debug_label
    };

    log_xr->info("create_quad_layer(): created {}x{} quad swapchain '{}'", width, height, debug_label);
    return std::make_unique<Quad_layer>(*this, std::move(swapchain), width, height);
}

void Xr_session::register_quad_layer(Quad_layer* quad_layer)
{
    if (quad_layer == nullptr) {
        return;
    }
    if (std::find(m_quad_layers.begin(), m_quad_layers.end(), quad_layer) == m_quad_layers.end()) {
        m_quad_layers.push_back(quad_layer);
    }
}

void Xr_session::unregister_quad_layer(Quad_layer* quad_layer)
{
    m_quad_layers.erase(
        std::remove(m_quad_layers.begin(), m_quad_layers.end(), quad_layer),
        m_quad_layers.end()
    );
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

    // XR_FB_composition_layer_depth_test: the compositor's shared depth buffer is
    // cleared to "infinitely far" at the start of composition, and a layer writes
    // its depth into that buffer ONLY when it carries an XrCompositionLayerDepthTestFB
    // with depthMask=XR_TRUE. The depth-tested quads below compare against this shared
    // buffer, so the PROJECTION layer (whose per-pixel depth comes from the chained
    // XR_KHR_composition_layer_depth info) must itself write the scene depth into it --
    // otherwise the buffer stays at "far" and the quads are never occluded (the bug we
    // chased: depth swapchain + ENABLE_DEPTH_TEST flag both reach the compositor, yet
    // nothing populates the test buffer). The projection is the opaque base, so it
    // writes unconditionally (compareOp ALWAYS). Lives until the xrEndFrame call below.
    // Gated like the quad tests: FB extension present AND projection depth submitted.
    XrCompositionLayerDepthTestFB projection_depth_test{
        .type      = XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_FB,
        .next      = nullptr,
        .depthMask = XR_TRUE,
        .compareOp = XR_COMPARE_OP_ALWAYS_FB
    };
    if (m_instance.extensions.FB_composition_layer_depth_test && m_projection_depth_submitted) {
        projection_layer.next = &projection_depth_test;
    }

    // Quad composition layers. Built into stable local storage (reserved so the
    // pointers pushed into `layers` stay valid) and appended after the
    // projection layer so the runtime composites the UI on top of the scene.
    // Both this vector and `layers` outlive the xrEndFrame call below.
    std::vector<XrCompositionLayerQuad>        quad_layers;
    std::vector<XrCompositionLayerDepthTestFB> quad_depth_tests;

    std::vector<XrCompositionLayerBaseHeader*> layers;
    bool passthrough_layer_submitted = false;
    if (rendered) {
        // Gate on the running state, not the extension flag: passthrough may be
        // paused (editor scene running without session passthrough), and layer
        // creation may have failed (m_passthrough_layer_fb == XR_NULL_HANDLE).
        if (m_passthrough_running) {
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&passthrough_fb_layer));
            passthrough_layer_submitted = true;
        }
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection_layer));

        // Submit quad layers in composition order (ascending: a higher order
        // composites later, i.e. on top) so e.g. the Hud stays in front of the
        // Hotbar. Each visible quad contributes a front quad, plus a back-facing
        // quad when double-sided.
        std::vector<Quad_layer*> ordered_quad_layers = m_quad_layers;
        std::stable_sort(
            ordered_quad_layers.begin(), ordered_quad_layers.end(),
            [](const Quad_layer* a, const Quad_layer* b) {
                return a->get_composition_order() < b->get_composition_order();
            }
        );

        // quad_is_depth_tested[i] mirrors quad_layers[i]. Both quad_layers and
        // quad_depth_tests are reserved up front so they never reallocate, since
        // the depth-test structs are chained into quads by address below.
        quad_layers.reserve(ordered_quad_layers.size() * 2);
        std::vector<bool> quad_is_depth_tested;
        quad_is_depth_tested.reserve(ordered_quad_layers.size() * 2);
        for (Quad_layer* quad_layer : ordered_quad_layers) {
            const bool depth_tested = quad_layer->is_depth_tested();
            XrCompositionLayerQuad quad{};
            if (quad_layer->build_layer(m_xr_reference_space_stage, quad)) {
                quad_layers.push_back(quad);
                quad_is_depth_tested.push_back(depth_tested);
            }
            if (quad_layer->is_double_sided()) {
                XrCompositionLayerQuad back_quad{};
                if (quad_layer->build_back_layer(m_xr_reference_space_stage, back_quad)) {
                    quad_layers.push_back(back_quad);
                    quad_is_depth_tested.push_back(depth_tested);
                }
            }
        }

        // Depth-test the flagged quads (e.g. the Hud) against the scene by
        // chaining an XrCompositionLayerDepthTestFB, but only when the runtime
        // supports it AND the projection layer submitted depth this frame.
        const bool depth_test_quads =
            m_instance.extensions.FB_composition_layer_depth_test &&
            m_projection_depth_submitted;
        if (depth_test_quads) {
            std::size_t depth_tested_count = 0;
            for (std::size_t i = 0; i < quad_is_depth_tested.size(); ++i) {
                if (quad_is_depth_tested[i]) {
                    ++depth_tested_count;
                }
            }
            quad_depth_tests.reserve(depth_tested_count);
            for (std::size_t i = 0; i < quad_layers.size(); ++i) {
                if (!quad_is_depth_tested[i]) {
                    continue;
                }
                quad_depth_tests.push_back(
                    XrCompositionLayerDepthTestFB{
                        .type      = XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_FB,
                        .next      = nullptr,
                        .depthMask = XR_FALSE, // UI quads do not write depth
                        // The quad passes (is drawn) where it is at or nearer than
                        // the scene depth. Determined empirically on Quest: the FB
                        // depth test compares such that a nearer quad needs
                        // LESS_OR_EQUAL (GREATER_OR_EQUAL rejected the near HUD
                        // against the far/empty scene; ALWAYS confirmed the
                        // mechanism is otherwise fine). See
                        // doc/hud_hotbar_depth_test_plan.md.
                        .compareOp = XR_COMPARE_OP_LESS_OR_EQUAL_FB
                    }
                );
                quad_layers[i].next = &quad_depth_tests.back();
            }
        }

        for (XrCompositionLayerQuad& quad : quad_layers) {
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad));
        }
    }

    // Clear per-frame render state for every quad layer, regardless of whether
    // this frame was rendered, so a deferred/non-rendered frame cannot submit a
    // stale image on a later frame.
    for (Quad_layer* quad_layer : m_quad_layers) {
        quad_layer->reset_frame_state();
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

    // XR_META_boundary_visibility: suppress the boundary while the passthrough
    // layer is visible (the room already shows through, so the boundary grid
    // is just noise). The spec only allows suppression while passthrough is
    // rendered, so the request is retried each frame until the runtime accepts
    // it and confirms via XrEventDataBoundaryVisibilityChangedMETA (tracked in
    // m_boundary_visibility). Restoring is handled by set_passthrough_active()
    // and by the runtime itself when passthrough stops.
    if (passthrough_layer_submitted && (m_boundary_visibility != XR_BOUNDARY_VISIBILITY_SUPPRESSED_META)) {
        request_boundary_visibility(XR_BOUNDARY_VISIBILITY_SUPPRESSED_META);
    }

    return true;
}

} // namespace erhe::xr
