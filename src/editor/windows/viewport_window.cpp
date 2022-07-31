#include "windows/viewport_windows.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/post_processing.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_renderer.hpp"
#endif
#include "windows/viewport_window.hpp"

#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui_viewport.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/render_graph.hpp"
#include "erhe/application/view.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

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


int Viewport_window::s_serial = 0;

Viewport_window::Viewport_window(
    const std::string_view              name,
    const erhe::components::Components& components,
    const std::shared_ptr<Scene_root>&  scene_root,
    erhe::scene::Camera*                camera
)
    : Imgui_window                        {name, fmt::format("{}##{}", name, ++s_serial)}
    , erhe::application::Render_graph_node{name}
    , m_configuration                     {components.get<erhe::application::Configuration    >()}
    , m_pipeline_state_tracker            {components.get<erhe::graphics::OpenGL_state_tracker>()}
    , m_editor_rendering                  {components.get<Editor_rendering>()}
    , m_post_processing                   {components.get<Post_processing >()}
    , m_programs                          {components.get<Programs        >()}
    , m_viewport_config                   {components.get<Viewport_config >()}
    , m_name                              {name}
    , m_scene_root                        {scene_root}
    , m_camera                            {camera}
{
}

auto Viewport_window::get_override_shader_stages() const -> erhe::graphics::Shader_stages*
{
    switch (m_shader_stages_variant)
    {
        case Shader_stages_variant::standard:                 return m_programs->standard.get();
        case Shader_stages_variant::debug_depth:              return m_programs->debug_depth.get();
        case Shader_stages_variant::debug_normal:             return m_programs->debug_normal.get();
        case Shader_stages_variant::debug_tangent:            return m_programs->debug_tangent.get();
        case Shader_stages_variant::debug_bitangent:          return m_programs->debug_bitangent.get();
        case Shader_stages_variant::debug_texcoord:           return m_programs->debug_texcoord.get();
        case Shader_stages_variant::debug_vertex_color_rgb:   return m_programs->debug_vertex_color_rgb.get();
        case Shader_stages_variant::debug_vertex_color_alpha: return m_programs->debug_vertex_color_alpha.get();
        case Shader_stages_variant::debug_misc:               return m_programs->debug_misc.get();
        default:                                              return m_programs->standard.get();
    }
}

void Viewport_window::execute_render_graph_node()
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
        .override_shader_stages = get_override_shader_stages()
    };

#if defined(ERHE_RAYTRACE_LIBRARY_NONE)
    if (m_is_hovered)
    {
        m_editor_rendering->render_id(context);
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
        bind_framebuffer_main();
        if (!is_framebuffer_ready())
        {
            return;
        }

        static constexpr std::string_view c_clear{"clear"};
        ERHE_PROFILE_GPU_SCOPE(c_clear);
        clear();
    }
    else
    {
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);

        // Framebuffer has been cleared elsewhere
    }

    m_editor_rendering->render_viewport_main(context, m_is_hovered);

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

    // bind_framebuffer_overlay();
    // m_editor_rendering->render_viewport_overlay(context, m_is_hovered);
}

void Viewport_window::clear() const
{
    ERHE_PROFILE_FUNCTION

    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(Color_blend_state::color_blend_disabled);
    gl::clear_color(
        m_viewport_config->clear_color[0],
        m_viewport_config->clear_color[1],
        m_viewport_config->clear_color[2],
        m_viewport_config->clear_color[3]
    );
    gl::clear_stencil(0);
    gl::clear_depth_f(*m_configuration->depth_clear_value_pointer());
    gl::clear(
        gl::Clear_buffer_mask::color_buffer_bit |
        gl::Clear_buffer_mask::depth_buffer_bit |
        gl::Clear_buffer_mask::stencil_buffer_bit
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

void Viewport_window::set_viewport(erhe::application::Imgui_viewport* imgui_viewport)
{
    Imgui_window::set_viewport(imgui_viewport);
    imgui_viewport->add_dependency(this);
}

auto Viewport_window::should_post_process() const -> bool
{
    return
        m_configuration->graphics.post_processing &&
        m_enable_post_processing &&
        (m_shader_stages_variant == Shader_stages_variant::standard) &&
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

    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo(
        "Shader",
        reinterpret_cast<int*>(&m_shader_stages_variant),
        c_shader_stages_variant_strings,
        IM_ARRAYSIZE(c_shader_stages_variant_strings),
        IM_ARRAYSIZE(c_shader_stages_variant_strings)
    );
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
        SPDLOG_LOGGER_TRACE(
            log_render,
            "Viewport_window::imgui() rendering texture {} {}",
            texture->gl_name(),
            texture->debug_label()
        );
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

void Viewport_window::set_post_processing_enable(bool value)
{
    m_enable_post_processing = value;
}

auto Viewport_window::content_region_size() const -> glm::ivec2
{
    return m_content_region_size;
}

auto Viewport_window::is_hovered() const -> bool
{
    return m_is_hovered;
}

[[nodiscard]] auto Viewport_window::scene_root() const -> Scene_root*
{
    return m_scene_root.get();
}

auto Viewport_window::viewport() const -> const erhe::scene::Viewport&
{
    return m_viewport;
}

auto Viewport_window::camera() const -> erhe::scene::Camera*
{
    return m_camera;
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

void Viewport_window::bind_framebuffer_main()
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

void Viewport_window::bind_framebuffer_overlay()
{
    // TODO This is currently wrong.
    //
    //      To fix this: Take into account post processing;
    //      If post processing is enabled in the viewport,
    //      use post processing output as target instead.
    //
    //      Even better, insert overlay into correct place
    //      in the render graph.
    int draw_fbo_name = m_framebuffer_resolved
        ? m_framebuffer_resolved->gl_name()
        : m_framebuffer_multisample->gl_name();

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
) const -> nonstd::optional<glm::dvec3>
{
    if (m_camera == nullptr)
    {
        return {};
    }
    const auto camera_projection_transforms = m_camera->projection_transforms(m_viewport);
    constexpr double depth_range_near = 0.0;
    constexpr double depth_range_far  = 1.0;
    return erhe::toolkit::project_to_screen_space<double>(
        camera_projection_transforms.clip_from_world.matrix(),
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
    if (m_camera == nullptr)
    {
        return {};
    }
    const auto camera_projection_transforms = m_camera->projection_transforms(m_viewport);
    constexpr double depth_range_near = 0.0;
    constexpr double depth_range_far  = 1.0;
    return erhe::toolkit::unproject<double>(
        camera_projection_transforms.clip_from_world.inverse_matrix(),
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

#if !defined(ERHE_RAYTRACE_LIBRARY_NONE)
void Pointer_context::raytrace()
{
    ERHE_PROFILE_FUNCTION

    if (m_window == nullptr)
    {
        return;
    }

    const auto pointer_near = position_in_world_viewport_depth(1.0);
    const auto pointer_far  = position_in_world_viewport_depth(0.0);

    if (!pointer_near.has_value() || !pointer_far.has_value())
    {
        return;
    }

    const glm::vec3 origin{pointer_near.value()};

    auto& scene = m_scene_root->raytrace_scene();
    {
        erhe::toolkit::Scoped_timer scoped_timer{m_ray_scene_build_timer};
        scene.commit();
    }

    m_nearest_slot = 0;
    float nearest_t_far = 9999.0f;
    {
        ERHE_PROFILE_SCOPE("raytrace inner");

        erhe::toolkit::Scoped_timer scoped_timer{m_ray_traverse_timer};

        for (size_t i = 0; i < slot_count; ++i)
        {
            auto& entry = m_hover_entries[i];
            erhe::raytrace::Ray ray{
                .origin    = glm::vec3{pointer_near.value()},
                .t_near    = 0.0f,
                .direction = glm::vec3{glm::normalize(pointer_far.value() - pointer_near.value())},
                .time      = 0.0f,
                .t_far     = 9999.0f,
                .mask      = slot_masks[i],
                .id        = 0,
                .flags     = 0
            };
            erhe::raytrace::Hit hit;
            scene.intersect(ray, hit);
            entry.valid = hit.instance != nullptr;
            entry.mask  = slot_masks[i];
            if (entry.valid)
            {
                void* user_data     = hit.instance->get_user_data();
                entry.raytrace_node = reinterpret_cast<Node_raytrace*>(user_data);
                entry.position      = ray.origin + ray.t_far * ray.direction;
                entry.geometry      = nullptr;
                entry.local_index   = 0;

                SPDLOG_LOGGER_TRACE(log_pointer, "{}: Hit position: {}", slot_names[i], entry.position.value());

                if (entry.raytrace_node != nullptr)
                {
                    auto* node = entry.raytrace_node->get_node();
                    if (node != nullptr)
                    {
                        SPDLOG_LOGGER_TRACE(log_pointer, "{}: Hit node: {} {}", slot_names[i], node->node_type(), node->name());
                        const auto* rt_instance = entry.raytrace_node->raytrace_instance();
                        if (rt_instance != nullptr)
                        {
                            SPDLOG_LOGGER_TRACE(
                                log_pointer,
                                "{}: RT instance: {}",
                                slot_names[i],
                                rt_instance->is_enabled()
                                    ? "enabled"
                                    : "disabled"
                            );
                        }
                    }
                    if (is_mesh(node))
                    {
                        entry.mesh = as_mesh(node->shared_from_this());
                        auto* primitive = entry.raytrace_node->raytrace_primitive();
                        entry.primitive = 0; // TODO
                        if (primitive != nullptr)
                        {
                            entry.geometry = entry.raytrace_node->source_geometry().get();
                            if (entry.geometry != nullptr)
                            {
                                SPDLOG_LOGGER_TRACE(log_pointer, "{}: Hit geometry: {}", slot_names[i], entry.geometry->name);
                            }
                            if (hit.primitive_id < primitive->primitive_geometry.primitive_id_to_polygon_id.size())
                            {
                                const auto polygon_id = primitive->primitive_geometry.primitive_id_to_polygon_id[hit.primitive_id];
                                SPDLOG_LOGGER_TRACE(log_pointer, "{}: Hit polygon: {}", slot_names[i], polygon_id);
                                entry.local_index = polygon_id;
                            }
                        }
                    }
                }

                if (ray.t_far < nearest_t_far)
                {
                    m_nearest_slot = i;
                    nearest_t_far = ray.t_far;
                }
            }
            else
            {
                SPDLOG_LOGGER_TRACE(log_pointer, "{}: no hit", slot_names[i]);
            }
        }
    }

    SPDLOG_LOGGER_TRACE(log_pointer, "Nearest slot: {}", slot_names[m_nearest_slot]);

    const auto duration = m_ray_traverse_timer.duration().value();
    if (duration >= std::chrono::milliseconds(1))
    {
        SPDLOG_LOGGER_TRACE(log_pointer, "ray intersect time: {}", std::chrono::duration_cast<std::chrono::milliseconds>(duration));
    }
    else if (duration >= std::chrono::microseconds(1))
    {
        SPDLOG_LOGGER_TRACE(log_pointer, "ray intersect time: {}", std::chrono::duration_cast<std::chrono::microseconds>(duration));
    }
    else
    {
        SPDLOG_LOGGER_TRACE(log_pointer, "ray intersect time: {}", duration);
    }

#if 0
    erhe::scene::Mesh*         hit_mesh      {nullptr};
    erhe::geometry::Geometry*  hit_geometry  {nullptr};
    erhe::geometry::Polygon_id hit_polygon_id{0};
    float                      hit_t         {std::numeric_limits<float>::max()};
    float                      hit_u         {0.0f};
    float                      hit_v         {0.0f};
    const auto& content_layer = m_scene_root->content_layer();
    for (auto& mesh : content_layer.meshes)
    {
        erhe::geometry::Geometry*  geometry  {nullptr};
        erhe::geometry::Polygon_id polygon_id{0};
        float                      t         {std::numeric_limits<float>::max()};
        float                      u         {0.0f};
        float                      v         {0.0f};
        const bool hit = erhe::raytrace::intersect(
            *mesh.get(),
            origin,
            direction,
            geometry,
            polygon_id,
            t,
            u,
            v
        );
        if (hit)
        {
            log->frame_log(
                "hit mesh {}, t = {}, polygon id = {}",
                mesh->name(),
                t,
                static_cast<uint32_t>(polygon_id)
            );
        }
        if (hit && (t < hit_t))
        {
            hit_mesh       = mesh.get();
            hit_geometry   = geometry;
            hit_polygon_id = polygon_id;
            hit_t          = t;
            hit_u          = u;
            hit_v          = v;
        }
    }
    if (hit_t != std::numeric_limits<float>::max())
    {
        pointer_context.raytrace_hit_position = origin + hit_t * direction;
        if (hit_polygon_id < hit_geometry->polygon_count())
        {
            auto* polygon_normals = hit_geometry->polygon_attributes().find<glm::vec3>(c_polygon_normals);
            if ((polygon_normals != nullptr) && polygon_normals->has(hit_polygon_id))
            {
                auto local_normal    = polygon_normals->get(hit_polygon_id);
                auto world_from_node = hit_mesh->world_from_node();
                pointer_context.raytrace_local_index = static_cast<size_t>(hit_polygon_id);
                pointer_context.raytrace_hit_normal = glm::vec3{
                    world_from_node * glm::vec4{local_normal, 0.0f}
                };
            }
        }
    }
#endif
}
#endif

void Viewport_window::reset_pointer_context()
{
    std::fill(
        m_hover_entries.begin(),
        m_hover_entries.end(),
        Hover_entry{}
    );

    m_position_in_viewport  .reset();
    m_near_position_in_world.reset();
    m_far_position_in_world .reset();
}

void Viewport_window::update_pointer_context(
    Id_renderer&    id_renderer,
    const glm::vec2 position_in_viewport
)
{
    ERHE_PROFILE_FUNCTION

    const bool reverse_depth = viewport().reverse_depth;
    m_position_in_viewport   = position_in_viewport;
    m_near_position_in_world = position_in_world_viewport_depth(reverse_depth ? 1.0f : 0.0f);
    m_far_position_in_world  = position_in_world_viewport_depth(reverse_depth ? 0.0f : 1.0f);

    if (m_camera == nullptr)
    {
        return;
    }

#if !defined(ERHE_RAYTRACE_LIBRARY_NONE)
    if (pointer_in_content_area())
    {
        raytrace();
    }
#endif

#if defined(ERHE_RAYTRACE_LIBRARY_NONE)
    const bool in_content_area = pointer_in_content_area();
    if (!in_content_area)
    {
        return;
    }
    const auto id_query = id_renderer.get(
        static_cast<int>(position_in_viewport.x),
        static_cast<int>(position_in_viewport.y)
    );

    m_near_position_in_world = position_in_world_viewport_depth(reverse_depth ? 1.0f : 0.0f);
    m_far_position_in_world  = position_in_world_viewport_depth(reverse_depth ? 0.0f : 1.0f);

    Hover_entry entry;
    entry.position = position_in_world_viewport_depth(id_query.depth);
    entry.valid    = id_query.valid;

    SPDLOG_LOGGER_TRACE(log_pointer, "position in world = {}", entry.position.value());
    if (!id_query.valid)
    {
        SPDLOG_LOGGER_TRACE(log_pointer, "pointer context hover not valid");
        return;
    }

    entry.mesh        = id_query.mesh;
    entry.primitive   = id_query.mesh_primitive_index;
    entry.local_index = id_query.local_index;
    if (entry.mesh)
    {
        const auto& primitive = entry.mesh->mesh_data.primitives[entry.primitive];
        entry.geometry = primitive.source_geometry.get();
        entry.normal   = {};
        if (entry.geometry != nullptr)
        {
            const auto polygon_id = static_cast<erhe::geometry::Polygon_id>(entry.local_index);
            if (polygon_id < entry.geometry->get_polygon_count())
            {
                SPDLOG_LOGGER_TRACE(log_pointer, "hover polygon = {}", polygon_id);
                auto* const polygon_normals = entry.geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals);
                if (
                    (polygon_normals != nullptr) &&
                    polygon_normals->has(polygon_id)
                )
                {
                    const auto local_normal    = polygon_normals->get(polygon_id);
                    const auto world_from_node = entry.mesh->world_from_node();
                    entry.normal = glm::vec3{world_from_node * glm::vec4{local_normal, 0.0f}};
                    SPDLOG_LOGGER_TRACE(log_pointer, "hover normal = {}", entry.normal.value());
                }
            }
        }
    }

    using erhe::toolkit::test_all_rhs_bits_set;

    const uint64_t visibility_mask = id_query.mesh ? entry.mesh->get_visibility_mask() : 0;

    const bool hover_content      = id_query.mesh && test_all_rhs_bits_set(visibility_mask, erhe::scene::Node_visibility::content     );
    const bool hover_tool         = id_query.mesh && test_all_rhs_bits_set(visibility_mask, erhe::scene::Node_visibility::tool        );
    const bool hover_brush        = id_query.mesh && test_all_rhs_bits_set(visibility_mask, erhe::scene::Node_visibility::brush       );
    const bool hover_rendertarget = id_query.mesh && test_all_rhs_bits_set(visibility_mask, erhe::scene::Node_visibility::rendertarget);
    SPDLOG_LOGGER_TRACE(
        log_pointer,
        "hover mesh = {} primitive = {} local index {} {}{}{}{}",
        entry.mesh ? entry.mesh->name() : "()",
        entry.primitive,
        entry.local_index,
        hover_content      ? "content "      : "",
        hover_tool         ? "tool "         : "",
        hover_brush        ? "brush "        : "",
        hover_rendertarget ? "rendertarget " : ""
    );
    if (hover_content)
    {
        m_hover_entries[Hover_entry::content_slot] = entry;
    }
    if (hover_tool)
    {
        m_hover_entries[Hover_entry::tool_slot] = entry;
    }
    if (hover_brush)
    {
        m_hover_entries[Hover_entry::brush_slot] = entry;
    }
    if (hover_rendertarget)
    {
        m_hover_entries[Hover_entry::rendertarget_slot] = entry;
    }
    m_scene_root->update_pointer_for_rendertarget_nodes();
#endif
}

auto Viewport_window::near_position_in_world() const -> nonstd::optional<glm::vec3>
{
    return m_near_position_in_world;
}

auto Viewport_window::far_position_in_world() const -> nonstd::optional<glm::vec3>
{
    return m_far_position_in_world;
}

auto Viewport_window::position_in_world_distance(
    const float distance
) const -> nonstd::optional<glm::vec3>
{
    if (
        !m_near_position_in_world.has_value() ||
        !m_far_position_in_world.has_value()
    )
    {
        return {};
    }

    const glm::vec3 p0        = m_near_position_in_world.value();
    const glm::vec3 p1        = m_far_position_in_world.value();
    const glm::vec3 origin    = p0;
    const glm::vec3 direction = glm::normalize(p1 - p0);
    return origin + distance * direction;
}

auto Viewport_window::position_in_world_viewport_depth(
    const double viewport_depth
) const -> nonstd::optional<glm::dvec3>
{
    if (
        !m_position_in_viewport.has_value() ||
        (m_camera == nullptr)
    )
    {
        return {};
    }

    const double depth_range_near     = 0.0;
    const double depth_range_far      = 1.0;
    const auto   position_in_viewport = glm::dvec3{
        m_position_in_viewport.value().x,
        m_position_in_viewport.value().y,
        viewport_depth
    };
    const auto      vp                    = viewport();
    const auto      projection_transforms = m_camera->projection_transforms(vp);
    const glm::mat4 world_from_clip       = projection_transforms.clip_from_world.inverse_matrix();

    return erhe::toolkit::unproject(
        glm::dmat4{world_from_clip},
        position_in_viewport,
        depth_range_near,
        depth_range_far,
        static_cast<double>(vp.x),
        static_cast<double>(vp.y),
        static_cast<double>(vp.width),
        static_cast<double>(vp.height)
    );
}

auto Viewport_window::position_in_viewport() const -> nonstd::optional<glm::vec2>
{
    return m_position_in_viewport;
}

auto Viewport_window::pointer_in_content_area() const -> bool
{
    return
        m_position_in_viewport.has_value() &&
        (m_position_in_viewport.value().x >= 0) &&
        (m_position_in_viewport.value().y >= 0) &&
        (m_position_in_viewport.value().x < viewport().width) &&
        (m_position_in_viewport.value().y < viewport().height);
}

auto Viewport_window::get_hover(size_t slot) const -> const Hover_entry&
{
    return m_hover_entries.at(slot);
}

auto Viewport_window::get_nearest_hover() const -> const Hover_entry&
{
    return m_hover_entries.at(m_nearest_slot);
}

} // namespace editor
