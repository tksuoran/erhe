#include "windows/viewport_window.hpp"

#include "log.hpp"
#include "editor_rendering.hpp"
#include "renderers/post_processing.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_renderer.hpp"
#endif

#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{


using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;
using erhe::graphics::Framebuffer;
using erhe::graphics::Renderbuffer;
using erhe::graphics::Texture;

auto Open_new_viewport_window_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_viewport_windows.open_new_viewport_window();
}

Viewport_windows::Viewport_windows()
    : erhe::components::Component       {c_label}
    , m_open_new_viewport_window_command{*this}
{
}

Viewport_windows::~Viewport_windows() noexcept
{
}

void Viewport_windows::declare_required_components()
{
    m_configuration          = require<erhe::application::Configuration    >();
    m_editor_view            = require<erhe::application::View             >();
    m_scene_root             = require<Scene_root      >();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    m_headset_renderer       = require<Headset_renderer>();
#endif

    // Need cameras to be setup
    require<erhe::application::Imgui_windows>();
    require<Scene_builder>();
}

void Viewport_windows::initialize_component()
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    {
        auto* const headset_camera = m_headset_renderer->root_camera().get();
        create_window("Headset Camera", headset_camera);
    }
#else
    {
        const auto& camera = m_scene_root->scene().cameras.front();
        auto* icamera = as_icamera(camera.get());
        create_window("Viewport", icamera);
    }
    //for (const auto& camera : m_scene_root->scene().cameras)
    //{
    //    auto* icamera = as_icamera(camera.get());
    //    create_window("Viewport", icamera);
    //}
#endif
    m_editor_view->register_command   (&m_open_new_viewport_window_command);
    m_editor_view->bind_command_to_key(&m_open_new_viewport_window_command, erhe::toolkit::Key_f1, true);
}

void Viewport_windows::post_initialize()
{
    m_editor_rendering       = get<Editor_rendering                    >();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
    m_pointer_context        = get<Pointer_context >();
    m_viewport_config        = get<Viewport_config >();
}

auto Viewport_windows::create_window(
    const std::string_view name,
    erhe::scene::ICamera*  camera
) -> Viewport_window*
{
    const auto new_window = std::make_shared<Viewport_window>(
        name,
        *m_components,
        camera
    );

    m_windows.push_back(new_window);

    auto& configuration = *m_configuration.get();

    if (configuration.imgui.enabled)
    {
        get<erhe::application::Imgui_windows>()->register_imgui_window(new_window.get());
    }
    return new_window.get();
}

auto Viewport_windows::open_new_viewport_window() -> bool
{
    if (m_scene_root->scene().cameras.empty())
    {
        return false;
    }
    auto selection_tool = get<Selection_tool>();
    for (const auto& entry : selection_tool->selection())
    {
        if (is_icamera(entry)) {
            auto* icamera = as_icamera(entry.get());
            create_window("Viewport", icamera);
            return true;
        }
    }
    const auto& camera = m_scene_root->scene().cameras.front();
    auto* icamera = as_icamera(camera.get());
    create_window("Viewport", icamera);
    return true;
}

void Viewport_windows::update_viewport_windows()
{
    ERHE_PROFILE_FUNCTION

    if (!m_configuration->imgui.enabled)
    {
        for (const auto& window : m_windows)
        {
            window->try_hover(
                static_cast<int>(m_pointer_context->mouse_x()),
                static_cast<int>(m_pointer_context->mouse_y())
            );
        }
    }

    bool any_window{false};
    for (const auto& window : m_windows)
    {
        window->update();
        if (window->is_hovered())
        {
            m_hover_window = window.get();
            any_window = true;
        }
    }

    if (any_window)
    {
        // Hack - check what is correct way to handle this
        if (!m_configuration->imgui.enabled)
        {
            m_editor_view->set_mouse_input_sink(m_hover_window);
        }

        m_pointer_context->update_viewport(m_hover_window);
    }
    else
    {
        m_pointer_context->update_viewport(nullptr);
    }
}

void Viewport_windows::render()
{
    ERHE_PROFILE_FUNCTION

    if (!m_configuration->imgui.enabled)
    {
        const int   total_width  = m_editor_view->width();
        const int   total_height = m_editor_view->height();
        const float count        = static_cast<float>(m_windows.size());
        size_t i = 0;
        for (const auto& window : m_windows)
        {
            const float start_rel = static_cast<float>(i) / count;
            const float width     = static_cast<float>(total_width) / count;
            const int   x         = static_cast<int>(start_rel * total_width);
            window->set_viewport(
                x,
                0,
                static_cast<int>(width),
                total_height
            );
            ++i;
        }

        m_editor_rendering->bind_default_framebuffer();
        m_editor_rendering->clear();
    }

    for (const auto& window : m_windows)
    {
        window->render(
            *m_editor_rendering.get(),
            *m_pipeline_state_tracker.get()
        );
    }
}

int Viewport_window::s_serial = 0;

Viewport_window::Viewport_window(
    const std::string_view              name,
    const erhe::components::Components& components,
    erhe::scene::ICamera*               camera
)
    : Imgui_window     {name, fmt::format("{}##{}", name, ++s_serial)}
    , m_configuration  {components.get<erhe::application::Configuration>()}
    , m_pointer_context{components.get<Pointer_context>()}
    , m_post_processing{components.get<Post_processing>()}
    , m_programs       {components.get<Programs       >()}
    , m_scene_root     {components.get<Scene_root     >()}
    , m_viewport_config{components.get<Viewport_config>()}
    , m_camera         {camera}
{
}

void Viewport_window::try_hover(const int px, const int py)
{
    ERHE_PROFILE_FUNCTION

    m_is_hovered = m_viewport.hit_test(px, py);
}

void Viewport_window::update()
{
    ERHE_PROFILE_FUNCTION

    if (m_camera != nullptr)
    {
        m_projection_transforms = m_camera->projection_transforms(m_viewport);
    }
}

void Viewport_window::render(
    Editor_rendering&                     editor_rendering,
    erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker
)
{
    ERHE_PROFILE_FUNCTION

    if (!should_render())
    {
        return;
    }

    const Render_context context
    {
        .window                 = this,
        .viewport_config        = m_viewport_config.get(),
        .camera                 = m_camera,
        .viewport               = m_viewport,
        .override_shader_stages = m_visualize_normal
            ? m_programs->visualize_normal.get()
            : m_visualize_tangent
                ? m_programs->visualize_tangent.get()
                : m_visualize_bitangent
                    ? m_programs->visualize_bitangent.get()
                    : nullptr
    };

#if defined(ERHE_RAYTRACE_LIBRARY_NONE)
    if (m_is_hovered)
    {
        editor_rendering.render_id(context);
    }
#endif

    if (
        m_configuration->imgui.enabled ||
        m_configuration->graphics.post_processing
    )
    {
        if (!is_framebuffer_ready())
        {
            update_framebuffer();
        }
        bind_framebuffer();
        if (!is_framebuffer_ready())
        {
            return;
        }

        static constexpr std::string_view c_clear{"clear"};
        ERHE_PROFILE_GPU_SCOPE(c_clear);
        clear(pipeline_state_tracker);
    }
    else
    {
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);

        // Framebuffer has been cleared elsewhere
    }

    editor_rendering.render_viewport(context, m_is_hovered);

    if (
        m_configuration->graphics.post_processing ||
        m_configuration->imgui.enabled
    )
    {
        resolve();
    }

    if (should_post_process())
    {
        m_post_processing->post_process(
            m_color_texture_resolved_for_present.get()
        );
    }
}

void Viewport_window::clear(
    erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker
) const
{
    ERHE_PROFILE_FUNCTION

    pipeline_state_tracker.shader_stages.reset();
    pipeline_state_tracker.color_blend.execute(Color_blend_state::color_blend_disabled);
    gl::clear_color(
        m_viewport_config->clear_color[0],
        m_viewport_config->clear_color[1],
        m_viewport_config->clear_color[2],
        m_viewport_config->clear_color[3]
    );
    gl::clear_depth_f(*m_configuration->depth_clear_value_pointer());
    gl::clear(
        gl::Clear_buffer_mask::color_buffer_bit |
        gl::Clear_buffer_mask::depth_buffer_bit
    );
}

auto Viewport_window::consumes_mouse_input() const -> bool
{
    return true;
}

void Viewport_window::on_begin()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
#endif
}

void Viewport_window::on_end()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PopStyleVar();
#endif
}

auto Viewport_window::should_post_process() const -> bool
{
    return
        m_configuration->graphics.post_processing &&
        m_enable_post_processing &&
        !m_visualize_normal &&
        !m_visualize_tangent &&
        !m_visualize_bitangent &&
        m_post_processing &&
        (
            !m_configuration->imgui.enabled ||
            m_viewport_config->post_processing_enable
        );
}

void Viewport_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    ImGui::SetNextItemWidth(150.0f);
    m_scene_root->camera_combo("Camera", m_camera);
    ImGui::SameLine();
    ImGui::Checkbox("Normal", &m_visualize_normal);
    ImGui::SameLine();
    ImGui::Checkbox("Tangent", &m_visualize_tangent);
    ImGui::SameLine();
    ImGui::Checkbox("Bitangent", &m_visualize_bitangent);
    ImGui::SameLine();
    ImGui::Checkbox("Post Processing", &m_enable_post_processing);

    //if (ImGui::Button("Post Process"))
    const auto size = ImGui::GetContentRegionAvail();

    if (
        m_can_present &&
        m_color_texture_resolved_for_present &&
        (m_color_texture_resolved_for_present->width() > 0) &&
        (m_color_texture_resolved_for_present->height() > 0)
    )
    {
        const bool use_post_processing_texture =
            should_post_process() &&
            m_post_processing->get_output();
        const auto& texture = use_post_processing_texture
            ? m_post_processing->get_output()
            : m_color_texture_resolved_for_present;
        image(
            texture,
            static_cast<int>(size.x),
            static_cast<int>(size.y)
            //ImVec2{0, 1},
            //ImVec2{1, 0}
        );
        m_content_region_min  = glm::vec2{ImGui::GetItemRectMin()};
        m_content_region_max  = glm::vec2{ImGui::GetItemRectMax()};
        m_content_region_size = glm::vec2{ImGui::GetItemRectSize()};
        m_is_hovered          = ImGui::IsItemHovered();
    }
    else
    {
        m_content_region_size = glm::vec2{size};
        m_is_hovered = false;
    }

    m_viewport.width  = m_content_region_size.x;
    m_viewport.height = m_content_region_size.y;

    //m_viewport_config.imgui();

    update_framebuffer();
#endif
}

void Viewport_window::set_viewport(
    const int x,
    const int y,
    const int width,
    const int height
)
{
    m_content_region_min.x  = x;
    m_content_region_min.y  = y;
    m_content_region_max.x  = x + width;
    m_content_region_max.y  = y + height;
    m_content_region_size.x = width;
    m_content_region_size.y = height;
    m_viewport.x            = x;
    m_viewport.y            = y;
    m_viewport.width        = width;
    m_viewport.height       = height;
}

auto Viewport_window::content_region_size() const -> glm::ivec2
{
    return m_content_region_size;
}

auto Viewport_window::is_hovered() const -> bool
{
    return m_is_hovered;
}

auto Viewport_window::viewport() const -> const erhe::scene::Viewport&
{
    return m_viewport;
}

auto Viewport_window::camera() const -> erhe::scene::ICamera*
{
    return m_camera;
}

auto Viewport_window::pointer_context() const -> const std::shared_ptr<Pointer_context>&
{
    return m_pointer_context;
}

auto Viewport_window::is_framebuffer_ready() const -> bool
{
    return
        (
            (m_configuration->graphics.msaa_sample_count > 0) &&
            m_framebuffer_multisample
        ) ||
        m_framebuffer_resolved;
}

void Viewport_window::bind_framebuffer()
{
    int draw_fbo_name = (m_configuration->graphics.msaa_sample_count > 0)
        ? m_framebuffer_multisample->gl_name()
        : m_framebuffer_resolved->gl_name();

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, draw_fbo_name);
#if !defined(NDEBUG)
    const auto status = gl::check_named_framebuffer_status(
        draw_fbo_name,
        gl::Framebuffer_target::draw_framebuffer
    );
    if (status != gl::Framebuffer_status::framebuffer_complete)
    {
        log_framebuffer->error("draw framebuffer status = {}", c_str(status));
    }
    ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
#endif
}

static constexpr std::string_view c_multisample_resolve{"Viewport_window::multisample_resolve()"};

void Viewport_window::resolve()
{
    ERHE_PROFILE_FUNCTION

    ERHE_PROFILE_GPU_SCOPE(c_multisample_resolve);

    if (m_configuration->graphics.msaa_sample_count > 0)
    {
        if (!m_framebuffer_multisample || !m_framebuffer_resolved)
        {
            return;
        }

        erhe::graphics::Scoped_debug_group pass_scope{c_multisample_resolve};

        {
            ERHE_PROFILE_SCOPE("bind read")
            gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, m_framebuffer_multisample->gl_name());
        }

#if !defined(NDEBUG)
        if constexpr (true)
        {
            const auto status = gl::check_named_framebuffer_status(
                m_framebuffer_multisample->gl_name(),
                gl::Framebuffer_target::draw_framebuffer
            );
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_framebuffer->error("read framebuffer status = {}", c_str(status));
            }
            ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
        }
#endif

        {
            ERHE_PROFILE_SCOPE("bind draw")
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer_resolved->gl_name());
        }

#if !defined(NDEBUG)
        if constexpr (true)
        {
            const auto status = gl::check_named_framebuffer_status(
                m_framebuffer_multisample->gl_name(),
                gl::Framebuffer_target::draw_framebuffer
            );
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_framebuffer->error("draw framebuffer status = {}", c_str(status));
            }
            ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
        }
#endif

        gl::disable(gl::Enable_cap::scissor_test);
        {
            ERHE_PROFILE_SCOPE("blit")
            gl::blit_framebuffer(
                0, 0, m_color_texture_multisample->width(), m_color_texture_multisample->height(),
                0, 0, m_color_texture_resolved   ->width(), m_color_texture_resolved   ->height(),
                gl::Clear_buffer_mask::color_buffer_bit, gl::Blit_framebuffer_filter::nearest
            );
        }
    }

    m_color_texture_resolved_for_present = m_color_texture_resolved;
}

void Viewport_window::update_framebuffer()
{
    ERHE_PROFILE_FUNCTION

    if (
        (m_content_region_size.x < 1) ||
        (m_content_region_size.y < 1)
    )
    {
        return;
    }

    if (
        (
            !(m_configuration->graphics.msaa_sample_count > 0) ||
            m_framebuffer_multisample
        ) &&
        m_framebuffer_resolved    &&
        m_color_texture_resolved  &&
        (m_color_texture_resolved->width()  == m_content_region_size.x) &&
        (m_color_texture_resolved->height() == m_content_region_size.y)
    )
    {
        return;
    }

    gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, 0);

    if (m_configuration->graphics.msaa_sample_count > 0)
    {
        m_color_texture_multisample = std::make_unique<Texture>(
            Texture::Create_info{
                .target = (m_configuration->graphics.msaa_sample_count > 0)
                    ? gl::Texture_target::texture_2d_multisample
                    : gl::Texture_target::texture_2d,
                .internal_format = m_configuration->graphics.low_hdr
                    ? gl::Internal_format::r11f_g11f_b10f
                    : gl::Internal_format::rgba16f,
                //.internal_format = gl::Internal_format::rgba32f,
                .sample_count    = m_configuration->graphics.msaa_sample_count,
                .width           = m_content_region_size.x,
                .height          = m_content_region_size.y
            }
        );
        m_color_texture_multisample->set_debug_label("Viewport Window Multisample Color");
        const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
        gl::clear_tex_image(
            m_color_texture_multisample->gl_name(),
            0,
            gl::Pixel_format::rgba,
            gl::Pixel_type::float_,
            &clear_value[0]
        );

        m_depth_stencil_multisample_renderbuffer = std::make_unique<Renderbuffer>(
            gl::Internal_format::depth24_stencil8,
            m_configuration->graphics.msaa_sample_count,
            m_content_region_size.x,
            m_content_region_size.y
        );

        m_depth_stencil_multisample_renderbuffer->set_debug_label("Viewport Window multisample Depth-Stencil");
        {
            Framebuffer::Create_info create_info;
            create_info.attach(gl::Framebuffer_attachment::color_attachment0,  m_color_texture_multisample.get());
            create_info.attach(gl::Framebuffer_attachment::depth_attachment,   m_depth_stencil_multisample_renderbuffer.get());
            create_info.attach(gl::Framebuffer_attachment::stencil_attachment, m_depth_stencil_multisample_renderbuffer.get());
            m_framebuffer_multisample = std::make_unique<Framebuffer>(create_info);
            m_framebuffer_multisample->set_debug_label("Viewport Window Multisample");

            gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0 };
            gl::named_framebuffer_draw_buffers(m_framebuffer_multisample->gl_name(), 1, &draw_buffers[0]);
            gl::named_framebuffer_read_buffer (m_framebuffer_multisample->gl_name(), gl::Color_buffer::color_attachment0);

            log_framebuffer->trace("Multisample framebuffer:");
            if (!m_framebuffer_multisample->check_status())
            {
                log_framebuffer->error("Multisample framebuffer not complete");
                m_framebuffer_multisample.reset();
            }
        }
    }

    {
        m_color_texture_resolved = std::make_shared<Texture>(
            Texture::Create_info{
                .target          = gl::Texture_target::texture_2d,
                .internal_format = m_configuration->graphics.low_hdr
                    ? gl::Internal_format::r11f_g11f_b10f
                    : gl::Internal_format::rgba16f,
                //.internal_format = gl::Internal_format::rgba32f,
                .width           = m_content_region_size.x,
                .height          = m_content_region_size.y
            }
        );
        m_color_texture_resolved->set_debug_label("Viewport Window Multisample Resolved");

        if (m_configuration->graphics.msaa_sample_count == 0)
        {
            m_depth_stencil_resolved_renderbuffer = std::make_unique<Renderbuffer>(
                gl::Internal_format::depth24_stencil8,
                0,
                m_content_region_size.x,
                m_content_region_size.y
            );
            m_depth_stencil_resolved_renderbuffer->set_debug_label("Viewport Window resolved Depth-Stencil");
        }

        if (!m_can_present)
        {
            constexpr float clear_value[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
            gl::clear_tex_image(
                m_color_texture_resolved->gl_name(),
                0,
                gl::Pixel_format::rgba,
                gl::Pixel_type::float_,
                &clear_value[0]
            );
            m_can_present = true;
        }
    }

    {
        Framebuffer::Create_info create_info;
        create_info.attach(
            gl::Framebuffer_attachment::color_attachment0,
            m_color_texture_resolved.get()
        );

        if (m_configuration->graphics.msaa_sample_count == 0)
        {
            create_info.attach(gl::Framebuffer_attachment::depth_attachment,   m_depth_stencil_resolved_renderbuffer.get());
            create_info.attach(gl::Framebuffer_attachment::stencil_attachment, m_depth_stencil_resolved_renderbuffer.get());
        }

        m_framebuffer_resolved = std::make_unique<Framebuffer>(create_info);
        m_framebuffer_resolved->set_debug_label("Viewport Window Multisample Resolved");

        constexpr gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0};
        gl::named_framebuffer_draw_buffers(m_framebuffer_resolved->gl_name(), 1, &draw_buffers[0]);

        log_framebuffer->trace("Multisample resolved framebuffer:");
        if (!m_framebuffer_resolved->check_status())
        {
            log_framebuffer->error("Multisample resolved framebuffer not complete");
            m_framebuffer_resolved.reset();
        }
    }
}

auto Viewport_window::to_scene_content(
    const glm::vec2 position_in_root
) const -> glm::vec2
{
    const float content_x      = static_cast<float>(position_in_root.x) - m_content_region_min.x;
    const float content_y      = static_cast<float>(position_in_root.y) - m_content_region_min.y;
    const float content_flip_y = m_content_region_size.y - content_y;
    return {
        content_x,
        content_flip_y
    };
}

auto Viewport_window::project_to_viewport(
    const glm::dvec3 position_in_world
) const -> glm::dvec3
{
    constexpr double depth_range_near = 0.0;
    constexpr double depth_range_far  = 1.0;
    return erhe::toolkit::project_to_screen_space<double>(
        m_projection_transforms.clip_from_world.matrix(),
        position_in_world,
        depth_range_near,
        depth_range_far,
        static_cast<double>(m_viewport.x),
        static_cast<double>(m_viewport.y),
        static_cast<double>(m_viewport.width),
        static_cast<double>(m_viewport.height)
    );
}

auto Viewport_window::unproject_to_world(
    const glm::dvec3 position_in_window
) const -> nonstd::optional<glm::dvec3>
{
    constexpr double depth_range_near = 0.0;
    constexpr double depth_range_far  = 1.0;
    return erhe::toolkit::unproject<double>(
        m_projection_transforms.clip_from_world.inverse_matrix(),
        position_in_window,
        depth_range_near,
        depth_range_far,
        static_cast<double>(m_viewport.x),
        static_cast<double>(m_viewport.y),
        static_cast<double>(m_viewport.width),
        static_cast<double>(m_viewport.height)
    );
}

auto Viewport_window::should_render() const -> bool
{
    return
        (m_viewport.width  > 0) &&
        (m_viewport.height > 0) &&
        (
            !m_configuration->imgui.enabled ||
            is_framebuffer_ready()
        ) &&
        (m_camera != nullptr);
}

} // namespace editor
