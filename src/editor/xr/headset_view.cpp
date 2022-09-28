#include "xr/headset_view.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "scene/material_library.hpp"
#include "tools/hotbar.hpp"
#include "tools/hud.hpp"
#include "tools/tools.hpp"
#include "windows/viewport_config.hpp"
#include "xr/controller_visualization.hpp"
#include "xr/hand_tracker.hpp"

#include "erhe/application/application.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
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

[[nodiscard]] auto Headset_view::get_type() const -> int
{
    return Input_context_type_headset_view;
}

Headset_view::Headset_view()
    : erhe::components::Component        {c_name}
    , erhe::application::Imgui_window    {c_description}
    , erhe::application::Rendergraph_node{c_description}
{
}

Headset_view::~Headset_view()
{
}

void Headset_view::declare_required_components()
{
    m_configuration = require<erhe::application::Configuration>();
    require<erhe::application::Imgui_windows>();
    require<erhe::application::Window>();
    require<Mesh_memory    >();
    require<Scene_builder  >();
    require<Shadow_renderer>();
    require<Tools          >();
}

void Headset_view::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    const auto scene_builder = get<Scene_builder>();
    m_scene_root = scene_builder->get_scene_root();

    setup_root_camera();

    if (!get<erhe::application::Configuration>()->headset.openxr)
    {
        return;
    }

    m_headset = std::make_unique<erhe::xr::Headset>(
        get<erhe::application::Window>()->get_context_window()
    );

    const auto mesh_memory = get<Mesh_memory>();
    m_controller_visualization = std::make_unique<Controller_visualization>(
        *mesh_memory,
        *m_scene_root,
        m_root_camera.get()
    );

    get<Tools>()->register_background_tool(this);

    //hide();
}

void Headset_view::post_initialize()
{
    m_commands               = get<erhe::application::Commands         >();
    m_line_renderer_set      = get<erhe::application::Line_renderer_set>();
    m_text_renderer          = get<erhe::application::Text_renderer    >();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();

    m_editor_rendering = get<Editor_rendering>();
    m_hand_tracker     = get<Hand_tracker    >();
    m_hud              = get<Hud             >();
    m_tools            = get<Tools           >();
}

auto Headset_view::description() -> const char*
{
    return c_description.data();
}

void Headset_view::tool_render(const Render_context& context)
{
    static_cast<void>(context);

    const auto transform     = m_root_camera->world_from_node();
    auto&      line_renderer = m_line_renderer_set->hidden;

    constexpr uint32_t red   = 0xff0000ffu;
    constexpr uint32_t green = 0xff00ff00u;
    constexpr uint32_t blue  = 0xffff0000u;
    constexpr uint32_t cyan  = 0xffffff00u;
    constexpr uint32_t gray  = 0x8888ccffu;

    line_renderer.set_thickness(4.0f);
    for (const auto& finger_input : m_finger_inputs)
    {
        const uint32_t color = m_mouse_down
            ? green
            : red;
        line_renderer.set_line_color(color);
        line_renderer.add_lines({{finger_input.finger_point, finger_input.point}});
    }

    for (const auto& controller_input : m_controller_inputs)
    {
        line_renderer.set_line_color(m_mouse_down ? cyan : blue);
        line_renderer.add_lines(
            {
                {
                    controller_input.position,
                    controller_input.position + controller_input.trigger_value * controller_input.direction
                }
            }
        );
        if (controller_input.trigger_value < 1.0f)
        {
            line_renderer.set_line_color(gray);
            line_renderer.add_lines(
                {
                    {
                        controller_input.position + controller_input.trigger_value * controller_input.direction,
                        controller_input.position + controller_input.direction
                    }
                }
            );
        }
    }
}

auto Headset_view::get_headset_view_resources(
    erhe::xr::Render_view& render_view
) -> std::shared_ptr<Headset_view_resources>
{
    auto match_color_texture = [&render_view](const auto& i)
    {
        return i->color_texture->gl_name() == render_view.color_texture;
    };

    const auto i = std::find_if(
        m_view_resources.begin(),
        m_view_resources.end(),
        match_color_texture
    );

    if (i == m_view_resources.end())
    {
        auto resource = std::make_shared<Headset_view_resources>(
            render_view,                               // erhe::xr::Render_view& render_view,
            *this,                                     // Headset_view&          headset_view,
            m_scene_root,                              // Scene_root&            scene_root,
            static_cast<std::size_t>(render_view.slot) // const std::size_t      slot
        );
        const std::string label = fmt::format(
            "Headset Viewport_window slot {} color texture {}",
            render_view.slot,
            render_view.color_texture
        );
        resource->viewport_window = std::make_shared<Viewport_window>(
            label,
            *m_components,
            m_scene_root,
            resource->camera
        );
        m_rendergraph->connect(
            erhe::application::Rendergraph_node_key::shadow_maps,
            m_shadow_render_node,
            resource->viewport_window
        );

        m_view_resources.push_back(resource);
        return resource;
    }
    return *i;
}

static constexpr std::string_view c_id_headset_clear{"HS clear"};
static constexpr std::string_view c_id_headset_render_content{"HS render content"};

void Headset_view::update_pointer_context_from_controller()
{
    const auto& pose                   = m_headset->controller_pose();
    //const float trigger_value          = m_headset->trigger_value();
    const auto  controller_orientation = glm::mat4_cast(pose.orientation);
    const auto  controller_direction   = glm::vec3{controller_orientation * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}};

    //if (trigger_value == 0.0f)
    //{
    //    return;
    //}
    const glm::vec3 ray_origin    = pose.position;
    const glm::vec3 ray_direction = controller_direction;

    raytrace_update(
        ray_origin,
        ray_direction,
        m_tools->get_tool_scene_root().lock().get()
    );
}

void Headset_view::execute_rendergraph_node()
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
            erhe::application::Configuration*     configuration;
            erhe::application::Line_renderer_set* line_renderer_set;
            erhe::application::Text_renderer*     text_renderer;
        };
        Context context
        {
            .pipeline_state_tracker = m_pipeline_state_tracker.get(),
            .configuration          = m_configuration.get(),
            .line_renderer_set      = m_line_renderer_set.get(),
            .text_renderer          = m_text_renderer.get()
        };

        auto callback = [this, &context](erhe::xr::Render_view& render_view) -> bool
        {
            const auto& view_resources = get_headset_view_resources(render_view);
            if (!view_resources->is_valid)
            {
                return false;
            }

            view_resources->update(render_view);

            auto* framebuffer = view_resources->framebuffer.get();
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer->gl_name());

            auto status = gl::check_named_framebuffer_status(framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_headset->error("view framebuffer status = {}", gl::c_str(status));
            }

            const erhe::scene::Viewport viewport
            {
                .x             = 0,
                .y             = 0,
                .width         = static_cast<int>(render_view.width),
                .height        = static_cast<int>(render_view.height),
                .reverse_depth = m_configuration->graphics.reverse_depth
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

            }

            if (m_headset->squeeze_click())
            {
                gl::clear_color(0.0f, 0.0f, 0.0f, 0.0f);
                gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
            }
            else
            {
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

                Viewport_config viewport_config;

                //const auto& layers           = m_scene_root->layers();
                //const auto& material_library = m_scene_root->material_library();
                //const auto& materials        = material_library->materials();

                Render_context render_context {
                    .scene_view      = this,
                    .viewport_config = &viewport_config,
                    .camera          = as_camera(view_resources->camera.get()),
                    .viewport        = viewport
                };

                m_editor_rendering->render_content(render_context);
                m_editor_rendering->render_selection(render_context);
                m_editor_rendering->render_tool_meshes(render_context);
                m_editor_rendering->render_rendertarget_nodes(render_context);

                if (m_line_renderer_set) // && m_headset->trigger_value() > 0.0f)
                {
                    ERHE_PROFILE_GPU_SCOPE(c_id_headset_render_content)
                    m_line_renderer_set->begin();
                    m_tools->render_tools(render_context);
                    m_line_renderer_set->render(viewport, *render_context.camera);
                    m_line_renderer_set->end();
                }
            }

            return true;
        };
        m_headset->render(callback);
    }
    m_headset->end_frame();
}

void Headset_view::setup_root_camera()
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

    m_scene_root->scene().add(m_root_camera);
}

auto Headset_view::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

auto Headset_view::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_root_camera;
}

[[nodiscard]] auto Headset_view::get_shadow_render_node() const -> Shadow_render_node*
{
    return m_shadow_render_node.get();
}

void Headset_view::add_finger_input(
    const Finger_point& finger_input
)
{
    m_finger_inputs.push_back(finger_input);
}

void Headset_view::add_controller_input(const Controller_input& controller_input)
{
    m_controller_inputs.push_back(controller_input);
}

[[nodiscard]] auto Headset_view::finger_to_viewport_distance_threshold() const -> float
{
    return m_finger_to_viewport_distance_threshold;
}

[[nodiscard]] auto Headset_view::get_hand_tracker() const -> Hand_tracker*
{
    return m_hand_tracker.get();
}

[[nodiscard]] auto Headset_view::get_headset() const -> erhe::xr::Headset*
{
    return m_headset.get();
}

void Headset_view::begin_frame()
{
    if (!m_headset)
    {
        return;
    }

    m_commands->set_input_context(this);
    m_finger_inputs.clear();
    m_controller_inputs.clear();
    update_pointer_context_from_controller();
    const float trigger_value = m_headset->trigger_value();
    if (trigger_value > 0.0f)
    {
        m_commands->on_controller_trigger(trigger_value);
    }
    if (m_controller_visualization)
    {
        m_controller_visualization->update(m_headset->controller_pose());
    }
    if (m_hand_tracker)
    {
        m_hand_tracker->update_hands(*m_headset.get());
    }

    const glm::mat4 world_from_view = m_headset->get_view_in_world();

    const auto& hotbar = get<Hotbar>();
    if (hotbar)
    {
        hotbar->update_node_transform(world_from_view);
    }

    if (m_hud)
    {
        m_hud->update_node_transform(world_from_view);
    }
    m_tools->on_hover(this);
}

void Headset_view::connect(const std::shared_ptr<Shadow_render_node>& shadow_render_node)
{
    m_shadow_render_node = shadow_render_node;
}

void Headset_view::imgui()
{
    m_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);

    ImGui::ColorEdit4(
        "Clear Color",
        &m_clear_color[0],
        ImGuiColorEditFlags_Float
    );

    ImGui::SliderFloat("Finger Distance Threshold", &m_finger_to_viewport_distance_threshold, 0.01f, 0.50f);

    constexpr ImVec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
    constexpr ImVec4 green{0.0f, 1.0f, 0.0f, 1.0f};
    for (const auto& finger_input : m_finger_inputs)
    {
        const float  distance = glm::distance(finger_input.finger_point, finger_input.point);
        const ImVec4 color    = ImGui::IsMouseDown(ImGuiMouseButton_Left)
            ? green
            : red;
        const std::string text = fmt::format(
            "{} : {} - {}",
            distance,
            finger_input.finger_point,
            finger_input.point
        );
        ImGui::TextColored(color, "%s", text.c_str());
    }

    for (const auto& controller_input : m_controller_inputs)
    {
        ImGui::Text("Controller Trigger Value: %f", controller_input.trigger_value);
    }
}

}