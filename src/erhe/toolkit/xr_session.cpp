#include "erhe/toolkit/xr_session.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/xr_instance.hpp"
#include "erhe/toolkit/xr.hpp"
#include "erhe/toolkit/xr_swapchain_image.hpp"

#define GLFW_EXPOSE_NATIVE_WIN32 1
#define GLFW_EXPOSE_NATIVE_WGL   1
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32      1
//#define XR_USE_GRAPHICS_API_VULKAN 1
#define XR_USE_GRAPHICS_API_OPENGL 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace erhe::xr {

Xr_session::Xr_session(Xr_instance&                   instance,
                       erhe::toolkit::Context_window& context_window)
    : m_instance              {instance}
    , m_context_window        {context_window}
    , m_xr_session            {XR_NULL_HANDLE}
    , m_swapchain_color_format{gl::Internal_format::srgb8_alpha8}
    , m_swapchain_depth_format{gl::Internal_format::depth24_stencil8}
    , m_xr_reference_space    {XR_NULL_HANDLE}
    , m_xr_session_state      {XR_SESSION_STATE_VISIBLE}

{
    log_xr.trace("{}\n", __func__);

    if (instance.get_xr_instance() == XR_NULL_HANDLE)
    {
        log_xr.error("No XR Instance\n");
        return;
    }

    m_xr_views.resize(instance.get_xr_view_conriguration_views().size());

    if (!create_session())
    {
        return;
    }
    if (!enumerate_swapchain_formats())
    {
        return;
    }
    if (!enumerate_reference_spaces())
    {
        return;
    }
    if (!create_swapchains())
    {
        return;
    }
    if (!create_reference_space())
    {
        return;
    }
    if (!begin_session())
    {
        return;
    }
    if (!attach_actions())
    {
        return;
    }
}

auto Xr_session::create_session() -> bool
{
    log_xr.trace("{}\n", __func__);

    const auto xr_instance = m_instance.get_xr_instance();
    VERIFY(xr_instance != XR_NULL_HANDLE);

    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR{nullptr};

    if (!check("xrGetInstanceProcAddr",
               xrGetInstanceProcAddr(xr_instance,
                                     "xrGetOpenGLGraphicsRequirementsKHR",
                                     reinterpret_cast<PFN_xrVoidFunction*>(&xrGetOpenGLGraphicsRequirementsKHR))))
    {
        return false;
    }

    XrGraphicsRequirementsOpenGLKHR  xr_graphics_requirements_opengl;
    xr_graphics_requirements_opengl.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
    xr_graphics_requirements_opengl.next = nullptr;

    if (!check("xrGetOpenGLGraphicsRequirementsKHR",
               xrGetOpenGLGraphicsRequirementsKHR(xr_instance,
                                                  m_instance.get_xr_system_id(),
                                                  &xr_graphics_requirements_opengl)))
    {
        return false;
    }

    GLFWwindow* const glfw_window = m_context_window.get_glfw_window();
    if (glfw_window == nullptr)
    {
        log_xr.error("No GLFW window\n");
        return false;
    }

    XrGraphicsBindingOpenGLWin32KHR graphics_binding_opengl_win32;
    graphics_binding_opengl_win32.type  = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
    graphics_binding_opengl_win32.next  = nullptr;
    graphics_binding_opengl_win32.hDC   = GetDC(glfwGetWin32Window(glfw_window));
    graphics_binding_opengl_win32.hGLRC = glfwGetWGLContext(glfw_window);

    XrSessionCreateInfo session_create_info;
    session_create_info.type        = XR_TYPE_SESSION_CREATE_INFO;
    session_create_info.next        = reinterpret_cast<const XrBaseInStructure*>(&graphics_binding_opengl_win32);
    session_create_info.createFlags = 0;
    session_create_info.systemId    = m_instance.get_xr_system_id();

    if (!check("xrCreateSession",
               xrCreateSession(xr_instance, &session_create_info, &m_xr_session)))
    {
        return false;
    }
    if (m_xr_session == XR_NULL_HANDLE)
    {
        log_xr.error("xrCreateSession() created XR_NULL_HANDLE session\n");
        return false;
    }

    return true;
}

Xr_session::~Xr_session()
{
    log_xr.trace("{}\n", __func__);

    m_view_swapchains.clear();

    if (m_xr_session == XR_NULL_HANDLE)
    {
        return;
    }

    check("xrEndSession",     xrEndSession    (m_xr_session));
    check("xrDestroySession", xrDestroySession(m_xr_session));
}

auto Xr_session::get_xr_session() const -> XrSession
{
    return m_xr_session;
}

auto Xr_session::get_xr_reference_space() const -> XrSpace
{
    return m_xr_reference_space;
}

auto Xr_session::get_xr_frame_state() const -> const XrFrameState&
{
    return m_xr_frame_state;
}

int color_format_score(gl::Internal_format image_format)
{
    switch (image_format)
    {
        case gl::Internal_format::rgba8:          return 1;
        case gl::Internal_format::srgb8_alpha8:   return 2;
        case gl::Internal_format::rgb10_a2:       return 3;
        case gl::Internal_format::r11f_g11f_b10f: return 4;
        case gl::Internal_format::rgba16f:        return 5;
        default: return 0;
    }
}

auto Xr_session::enumerate_swapchain_formats() -> bool
{
    log_xr.trace("{}\n", __func__);

    uint32_t swapchain_format_count{0};

    if (!check("xrEnumerateSwapchainFormats",
               xrEnumerateSwapchainFormats(m_xr_session, 0, &swapchain_format_count, nullptr)))
    {
        return false;
    }

    std::vector<int64_t> swapchain_formats(swapchain_format_count);

    if (!check("xrEnumerateSwapchainFormats",
               xrEnumerateSwapchainFormats(m_xr_session,
                                           swapchain_format_count,
                                           &swapchain_format_count,
                                           swapchain_formats.data())))
    {
        return false;
    }

    log_xr.info("Swapchain formats:\n");
    int best_color_format_score{0};
    m_swapchain_color_format = gl::Internal_format::srgb8_alpha8;
    for (const auto swapchain_format : swapchain_formats)
    {
        const auto gl_internal_format = static_cast<gl::Internal_format>(swapchain_format);
        log_xr.info("    {}\n", gl::c_str(gl_internal_format));
        const int color_score = color_format_score(gl_internal_format);
        if (color_score > best_color_format_score)
        {
            best_color_format_score = color_score;
            m_swapchain_color_format = gl_internal_format;
        }
    }
    log_xr.info("Selected swapchain color format {}\n", gl::c_str(m_swapchain_color_format));

    return true;
}

auto Xr_session::enumerate_reference_spaces() -> bool
{
    log_xr.trace("{}\n", __func__);

    uint32_t reference_space_type_count{0};
    if (!check("xrEnumerateReferenceSpaces",
               xrEnumerateReferenceSpaces(m_xr_session,
                                          0,
                                          &reference_space_type_count,
                                          nullptr)))
    {
        return false;
    }

    m_xr_reference_space_types.resize(reference_space_type_count);
    if (!check("xrEnumerateReferenceSpaces",
               xrEnumerateReferenceSpaces(m_xr_session,
                                          reference_space_type_count,
                                          &reference_space_type_count,
                                          m_xr_reference_space_types.data())))
    {
        return false;
    }
    log_xr.info("Reference space types:\n");
    for (const auto reference_space_type : m_xr_reference_space_types)
    {
        log_xr.info("    {}\n", c_str(reference_space_type));
    }

    return true;
}

auto Xr_session::create_swapchains() -> bool
{
    log_xr.trace("{}\n", __func__);

    const auto& views = m_instance.get_xr_view_conriguration_views();
    m_view_swapchains.clear();
    for (const auto& view : views)
    {
        XrSwapchainCreateInfo swapchain_create_info;
        swapchain_create_info.type        = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        swapchain_create_info.next        = nullptr;
        swapchain_create_info.createFlags = 0;
        swapchain_create_info.usageFlags  = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        swapchain_create_info.format      = static_cast<int64_t>(m_swapchain_color_format);
        swapchain_create_info.arraySize   = 1;
        swapchain_create_info.width       = view.recommendedImageRectWidth;
        swapchain_create_info.height      = view.recommendedImageRectHeight;
        swapchain_create_info.mipCount    = 1;
        swapchain_create_info.faceCount   = 1;
        swapchain_create_info.sampleCount = view.recommendedSwapchainSampleCount;

        XrSwapchain color_swapchain{XR_NULL_HANDLE};

        if (!check("xrCreateSwapchain",
                   xrCreateSwapchain(m_xr_session, &swapchain_create_info, &color_swapchain)))
        {
            return false;
        }

        swapchain_create_info.usageFlags  = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        swapchain_create_info.format      = static_cast<int64_t>(m_swapchain_depth_format);
        XrSwapchain depth_swapchain{XR_NULL_HANDLE};

        if (!check("xrCreateSwapchain",
                   xrCreateSwapchain(m_xr_session, &swapchain_create_info, &depth_swapchain)))
        {
            return false;
        }
        m_view_swapchains.emplace_back(color_swapchain, depth_swapchain);
    }

    return true;
}

auto Xr_session::create_reference_space() -> bool
{
    XrReferenceSpaceCreateInfo reference_space_create_info;
    reference_space_create_info.type                               = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    reference_space_create_info.next                               = nullptr;
    reference_space_create_info.referenceSpaceType                 = XR_REFERENCE_SPACE_TYPE_LOCAL;
    reference_space_create_info.poseInReferenceSpace.orientation.x = 0.0f;
    reference_space_create_info.poseInReferenceSpace.orientation.y = 0.0f;
    reference_space_create_info.poseInReferenceSpace.orientation.z = 0.0f;
    reference_space_create_info.poseInReferenceSpace.orientation.w = 1.0f;
    reference_space_create_info.poseInReferenceSpace.position.x    = 0.0f;
    reference_space_create_info.poseInReferenceSpace.position.y    = 0.0f;
    reference_space_create_info.poseInReferenceSpace.position.z    = 0.0f;

    if (!check("xrCreateReferenceSpace",
               xrCreateReferenceSpace(m_xr_session,
                                      &reference_space_create_info,
                                      &m_xr_reference_space)))
    {
        return false;
    }

    return true;
}

auto Xr_session::begin_session() -> bool
{
    XrSessionBeginInfo session_begin_info;
    session_begin_info.type                         = XR_TYPE_SESSION_BEGIN_INFO;
    session_begin_info.next                         = nullptr;
    session_begin_info.primaryViewConfigurationType = m_instance.get_xr_view_configuration_type();

    if (!check("xrBeginSession",
               xrBeginSession(m_xr_session, &session_begin_info)))
    {
        return false;
    }

    // m_instance.set_environment_depth_estimation(m_session, true);

    return true;
}

auto Xr_session::attach_actions() -> bool
{
    XrSessionActionSetsAttachInfo session_action_sets_attach_info;
    session_action_sets_attach_info.type            = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    session_action_sets_attach_info.next            = nullptr;
    session_action_sets_attach_info.countActionSets = 1;
    session_action_sets_attach_info.actionSets      = &m_instance.actions.action_set;
    if (!check("xrAttachSessionActionSets",
               xrAttachSessionActionSets(m_xr_session, &session_action_sets_attach_info)))
    {
        return false;
    }

    XrActionSpaceCreateInfo action_space_create_info;
    action_space_create_info.type                            = XR_TYPE_ACTION_SPACE_CREATE_INFO;
    action_space_create_info.next                            = nullptr;
    action_space_create_info.action                          = m_instance.actions.aim_pose;
    action_space_create_info.subactionPath                   = XR_NULL_PATH;
    action_space_create_info.poseInActionSpace.position.x    = 0.0f;
    action_space_create_info.poseInActionSpace.position.y    = 0.0f;
    action_space_create_info.poseInActionSpace.position.z    = 0.0f;
    action_space_create_info.poseInActionSpace.orientation.x = 0.0f;
    action_space_create_info.poseInActionSpace.orientation.y = 0.0f;
    action_space_create_info.poseInActionSpace.orientation.z = 0.0f;
    action_space_create_info.poseInActionSpace.orientation.w = 1.0f;
    if (!check("xrCreateActionSpace",
               xrCreateActionSpace(m_xr_session,
                                   &action_space_create_info,
                                   &m_instance.actions.aim_pose_space)))
    {
        return false;
    }

    return true;
}

auto Xr_session::begin_frame() -> bool
{
    XrFrameBeginInfo frame_begin_info;
    frame_begin_info.type = XR_TYPE_FRAME_BEGIN_INFO;
    frame_begin_info.next = nullptr;
    {
        const auto result = xrBeginFrame(m_xr_session, &frame_begin_info);
        if (result == XR_SUCCESS)
        {
            return true;
        }
        if (result == XR_SESSION_LOSS_PENDING)
        {
            log_xr.error("TODO Handle XR_SESSION_LOSS_PENDING\n");
            return false;
        }
        if (result == XR_FRAME_DISCARDED)
        {
            log_xr.warn("TODO Handle XR_FRAME_DISCARDED\n");
            return true;
        }
        log_xr.error("xrBeginFrame() returned error {}\n", c_str(result));
        return false;
    }
}

auto Xr_session::wait_frame() -> XrFrameState*
{
    XrFrameWaitInfo frame_wait_info;
    frame_wait_info.type = XR_TYPE_FRAME_WAIT_INFO;
    frame_wait_info.next = nullptr;

    m_xr_frame_state.type                   = XR_TYPE_FRAME_STATE;
    m_xr_frame_state.next                   = nullptr;
    m_xr_frame_state.predictedDisplayTime   = 0;
    m_xr_frame_state.predictedDisplayPeriod = 0;
    m_xr_frame_state.shouldRender           = XR_FALSE;
    {
        const auto result = xrWaitFrame(m_xr_session, &frame_wait_info, &m_xr_frame_state);
        if (result == XR_SUCCESS)
        {
            return &m_xr_frame_state;
        }
        else if (result == XR_SESSION_LOSS_PENDING)
        {
            log_xr.error("TODO Handle XR_SESSION_LOSS_PENDING\n");
            return nullptr;
        }
        else
        {
            log_xr.error("xrWaitFrame() returned error {}\n", c_str(result));
            return nullptr;
        }
    }
}

auto Xr_session::render_frame(std::function<bool(Render_view&)> render_view_callback) -> bool
{
    XrViewState view_state;
    view_state.type           = XR_TYPE_VIEW_STATE;
    view_state.next           = nullptr;
    view_state.viewStateFlags = 0;
    uint32_t view_capacity_input{static_cast<uint32_t>(m_xr_views.size())};
    uint32_t view_count_output  {0};

    XrViewLocateInfo view_locate_info;
    view_locate_info.type        = XR_TYPE_VIEW_LOCATE_INFO;
    view_locate_info.next        = nullptr;
    view_locate_info.displayTime = m_xr_frame_state.predictedDisplayTime;
    view_locate_info.space       = m_xr_reference_space;

    if (!check("xrLocateViews",
               xrLocateViews(m_xr_session,
                             &view_locate_info,
                             &view_state,
                             view_capacity_input,
                             &view_count_output,
                             m_xr_views.data())))
    {
        return false;
    }
    VERIFY(view_count_output == view_capacity_input);

    const auto& view_configuration_views = m_instance.get_xr_view_conriguration_views();

    m_xr_composition_layer_projection_views.resize(view_count_output);
    m_xr_composition_layer_depth_infos     .resize(view_count_output);
    for (uint32_t i = 0; i < view_count_output; ++i)
    {
        auto& swapchain = m_view_swapchains[i];
        auto acquired_color_swapchain_image_opt = swapchain.color_swapchain.acquire();
        auto acquired_depth_swapchain_image_opt = swapchain.depth_swapchain.acquire();
        if (!acquired_color_swapchain_image_opt.has_value() || !swapchain.color_swapchain.wait())
        {
            return false;
        }
        if (!acquired_depth_swapchain_image_opt.has_value() || !swapchain.depth_swapchain.wait())
        {
            return false;
        }

        const auto& acquired_color_swapchain_image = acquired_color_swapchain_image_opt.value();
        const auto& acquired_depth_swapchain_image = acquired_depth_swapchain_image_opt.value();

        const uint32_t color_texture = acquired_color_swapchain_image.get_gl_texture();
        const uint32_t depth_texture = acquired_depth_swapchain_image.get_gl_texture();
        if ((color_texture == 0) || (depth_texture == 0))
        {
            return false;
        }

        Render_view render_view;
        render_view.orientation   = to_glm(m_xr_views[i].pose.orientation);
        render_view.position      = to_glm(m_xr_views[i].pose.position);
        render_view.fov_left      = m_xr_views[i].fov.angleLeft;
        render_view.fov_right     = m_xr_views[i].fov.angleRight;
        render_view.fov_up        = m_xr_views[i].fov.angleUp;
        render_view.fov_down      = m_xr_views[i].fov.angleDown;
        render_view.color_texture = color_texture;
        render_view.color_format  = m_swapchain_color_format;
        render_view.depth_texture = depth_texture;
        render_view.depth_format  = m_swapchain_depth_format;
        render_view.width         = view_configuration_views[i].recommendedImageRectWidth;
        render_view.height        = view_configuration_views[i].recommendedImageRectHeight;

        {
            const auto result = render_view_callback(render_view);
            if (result == false)
            {
                return false;
            }
        }

        auto& composition_layer_depth_info = m_xr_composition_layer_depth_infos[i];
        composition_layer_depth_info.type                      = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR;
        composition_layer_depth_info.next                      = nullptr;
        composition_layer_depth_info.subImage.swapchain        = swapchain.depth_swapchain.get_xr_swapchain();
        composition_layer_depth_info.subImage.imageRect.offset = { 0, 0 };
        composition_layer_depth_info.subImage.imageRect.extent = { static_cast<int32_t>(view_configuration_views[i].recommendedImageRectWidth),
                                                                   static_cast<int32_t>(view_configuration_views[i].recommendedImageRectHeight) };
        composition_layer_depth_info.minDepth                  = 0.0f;
        composition_layer_depth_info.maxDepth                  = 1.0f;
        composition_layer_depth_info.nearZ                     = render_view.far_depth;
        composition_layer_depth_info.farZ                      = render_view.near_depth;

        auto& composition_layer_projection_view = m_xr_composition_layer_projection_views[i];
        composition_layer_projection_view.type                      = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        composition_layer_projection_view.next                      = &composition_layer_depth_info;
        composition_layer_projection_view.pose                      = m_xr_views[i].pose;
        composition_layer_projection_view.fov                       = m_xr_views[i].fov;
        composition_layer_projection_view.subImage.swapchain        = swapchain.color_swapchain.get_xr_swapchain();
        composition_layer_projection_view.subImage.imageRect.offset = { 0, 0 };
        composition_layer_projection_view.subImage.imageRect.extent = { static_cast<int32_t>(view_configuration_views[i].recommendedImageRectWidth),
                                                                        static_cast<int32_t>(view_configuration_views[i].recommendedImageRectHeight) };
    }

    return true;
}

auto Xr_session::end_frame() -> bool
{
    //XrCompositionLayerDepthTestVARJO layer_depth;
    //layer_depth.type                = XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_VARJO;
    //layer_depth.next                = nullptr;
    //layer_depth.depthTestRangeNearZ = 0.1f;
    //layer_depth.depthTestRangeFarZ  = 0.5f;

    XrCompositionLayerProjection layer;
    layer.type       = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    layer.next       = nullptr;
    layer.space      = m_xr_reference_space;
    layer.viewCount  = static_cast<uint32_t>(m_xr_views.size());
    layer.views      = m_xr_composition_layer_projection_views.data();
    layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    
    XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)
    };

    XrFrameEndInfo frame_end_info;
    frame_end_info.type                 = XR_TYPE_FRAME_END_INFO;
    frame_end_info.next                 = nullptr;
    frame_end_info.displayTime          = m_xr_frame_state.predictedDisplayTime;
    frame_end_info.environmentBlendMode = m_instance.get_xr_environment_blend_mode();
    frame_end_info.layerCount           = 1;
    frame_end_info.layers               = layers;

    if (!check("xrEndFrame", xrEndFrame(m_xr_session, &frame_end_info)))
    {
        return false;
    }

    return true;
}

}
