#include "xr/headset_renderer.hpp"

#include "application.hpp"
#include "configuration.hpp"
#include "editor_imgui_windows.hpp"
#include "editor_tools.hpp"
#include "log.hpp"
#include "rendering.hpp"
#include "window.hpp"

#include "renderers/line_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_root.hpp"
#include "windows/log_window.hpp"
#include "xr/controller_visualization.hpp"
#include "xr/hand_tracker.hpp"

#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/xr/headset.hpp"

#include <imgui.h>

namespace editor
{

using erhe::graphics::Color_blend_state;

Headset_renderer::Headset_renderer()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_description}
{
}

Headset_renderer::~Headset_renderer() = default;

void Headset_renderer::connect()
{
    m_application       = get    <Application      >();
    m_editor_rendering  = get    <Editor_rendering >();
    m_editor_tools      = get    <Editor_tools     >();
    m_hand_tracker      = get    <Hand_tracker     >();
    m_line_renderer_set = get    <Line_renderer_set>();
    m_scene_root        = require<Scene_root       >();

    require<Configuration>();
    require<Editor_imgui_windows>();
    require<Window>();
}

void Headset_renderer::initialize_component()
{
    get<Editor_imgui_windows>()->register_imgui_window(this);

    setup_root_camera();

    if (!get<Configuration>()->openxr)
    {
        return;
    }

    m_headset = std::make_unique<erhe::xr::Headset>(
        get<Window>()->get_context_window()
    );

    const auto mesh_memory = get<Mesh_memory>();
    m_controller_visualization = std::make_unique<Controller_visualization>(
        *mesh_memory,
        *m_scene_root,
        m_root_camera.get()
    );
}

auto Headset_renderer::get_headset_view_resources(
    erhe::xr::Render_view& render_view
) -> Headset_view_resources&
{
    auto match_color_texture = [&render_view](const auto& i)
    {
        return i.color_texture->gl_name() == render_view.color_texture;
    };

    const auto i = std::find_if(m_view_resources.begin(), m_view_resources.end(), match_color_texture);
    if (i == m_view_resources.end())
    {
        auto& j = m_view_resources.emplace_back(
            render_view,
            *this,
            *m_editor_rendering,
            m_view_resources.size()
        );
        return j;
    }
    return *i;
}

static constexpr std::string_view c_id_headset_clear{"HS clear"};
static constexpr std::string_view c_id_headset_render_content{"HS render content"};

void Headset_renderer::render()
{
    ERHE_PROFILE_FUNCTION

    if (!m_headset)
    {
        return;
    }

    auto frame_timing = m_headset->begin_frame();
    if (frame_timing.should_render)
    {
        struct Context
        {
            erhe::graphics::OpenGL_state_tracker* pipeline_state_tracker;
            Configuration*                        configuration;
            Line_renderer_set*                    line_renderer_set;
            Text_renderer*                        text_renderer;
        };
        Context context
        {
            .pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>().get(),
            .configuration          = get<Configuration    >().get(),
            .line_renderer_set      = get<Line_renderer_set>().get(),
            .text_renderer          = get<Text_renderer    >().get()
        };

        auto callback = [this, &context](erhe::xr::Render_view& render_view) -> bool
        {
            auto& view_resources = get_headset_view_resources(render_view);
            if (!view_resources.is_valid)
            {
                return false;
            }

            view_resources.update(render_view);

            auto* framebuffer = view_resources.framebuffer.get();
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer->gl_name());

            auto status = gl::check_named_framebuffer_status(framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_headset.error("view framebuffer status = {}\n", c_str(status));
            }

            const erhe::scene::Viewport viewport
            {
                .x             = 0,
                .y             = 0,
                .width         = static_cast<int>(render_view.width),
                .height        = static_cast<int>(render_view.height),
                .reverse_depth = get<Configuration>()->reverse_depth
            };
            gl::viewport(0, 0, render_view.width, render_view.height);

            context.pipeline_state_tracker->shader_stages.reset();
            context.pipeline_state_tracker->color_blend.execute(Color_blend_state::color_blend_disabled);
            gl::viewport(
                viewport.x,
                viewport.y,
                viewport.width,
                viewport.height
            );
            gl::enable(gl::Enable_cap::framebuffer_srgb);

            {
                ERHE_PROFILE_GPU_SCOPE(c_id_headset_clear)

                gl::clear_color(
                    m_clear_color[0],
                    m_clear_color[1],
                    m_clear_color[2],
                    m_clear_color[3]
                );
                gl::clear_depth_f(*context.configuration->depth_clear_value_pointer());
                gl::clear(
                    gl::Clear_buffer_mask::color_buffer_bit |
                    gl::Clear_buffer_mask::depth_buffer_bit
                );
            }

            if (!m_headset->squeeze_click())
            {
                Viewport_config viewport_config;
                Render_context render_context {
                    .window          = nullptr,
                    .viewport_config = &viewport_config,
                    .camera          = as_icamera(view_resources.camera.get()),
                    .viewport        = viewport
                };

                m_editor_rendering->render_content(render_context);
                m_editor_tools->render_tools(render_context);

                if (m_line_renderer_set) // && m_headset->trigger_value() > 0.0f)
                {
                    ERHE_PROFILE_GPU_SCOPE(c_id_headset_render_content)
                    m_line_renderer_set->render(viewport, *render_context.camera);
                }
            }

            return true;
        };
        m_headset->render(callback);
    }
    m_headset->end_frame();
}

void Headset_renderer::setup_root_camera()
{
    m_root_camera = std::make_shared<erhe::scene::Camera>(
        "Headset Root Camera"
    );
    const glm::mat4 m = erhe::toolkit::create_look_at(
        glm::vec3{0.0f, 0.0f,  1.0f}, // eye
        glm::vec3{0.0f, 0.0f,  0.0f}, // look at
        glm::vec3{0.0f, 1.0f,  0.0f}  // up
    );
    m_root_camera->set_parent_from_node(m);
    auto& projection = *m_root_camera->projection();
    projection.fov_y           = glm::radians(35.0f);
    projection.projection_type = erhe::scene::Projection::Type::perspective_vertical;
    projection.z_near          = 0.03f;
    projection.z_far           = 200.0f;

    const auto scene_root = get<Scene_root>();
    scene_root->scene().cameras.push_back(m_root_camera);
    scene_root->scene().nodes.emplace_back(m_root_camera);
    scene_root->scene().nodes_sorted = false;
}

auto Headset_renderer::root_camera() -> std::shared_ptr<erhe::scene::Camera>
{
    return m_root_camera;
}

void Headset_renderer::begin_frame()
{
    if (m_controller_visualization && m_headset)
    {
        m_controller_visualization->update(m_headset->controller_pose());
    }
    if (m_hand_tracker && m_headset)
    {
        m_hand_tracker->update(*m_headset.get());
    }

}

void Headset_renderer::imgui()
{
    ImGui::ColorEdit4(
        "Clear Color",
        &m_clear_color[0],
        ImGuiColorEditFlags_Float
    );
}

}