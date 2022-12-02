// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/viewport_window.hpp"

#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "editor_rendering.hpp"
#include "renderers/id_renderer.hpp"
#include "rendergraph/post_processing.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/grid_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "tools/trs_tool.hpp"
#include "windows/physics_window.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif

#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/multisample_resolve.hpp"
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
#   include <imgui_internal.h>
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

[[nodiscard]] auto Viewport_window::get_type() const -> int
{
    return Input_context_type_viewport_window;
}

Viewport_window::Viewport_window(
    const std::string_view                      name,
    const erhe::components::Components&         components,
    const std::shared_ptr<Scene_root>&          scene_root,
    const std::shared_ptr<erhe::scene::Camera>& camera
)
    : erhe::application::Rendergraph_node{name}
    , m_configuration                    {components.get<erhe::application::Configuration    >()}
    , m_pipeline_state_tracker           {components.get<erhe::graphics::OpenGL_state_tracker>()}
    , m_editor_rendering                 {components.get<Editor_rendering>()}
    , m_grid_tool                        {components.get<Grid_tool       >()}
    , m_physics_window                   {components.get<Physics_window  >()}
    , m_post_processing                  {components.get<Post_processing >()}
    , m_programs                         {components.get<Programs        >()}
    , m_trs_tool                         {components.get<Trs_tool        >()}
    , m_viewport_config                  {components.get<Viewport_config >()}
    , m_name                             {name}
    , m_scene_root                       {scene_root}
    , m_tool_scene_root                  {components.get<Tools>()->get_tool_scene_root()}
    , m_camera                           {camera}
{
    register_input(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "shadow_maps",
        erhe::application::Rendergraph_node_key::shadow_maps
    );
    register_output(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        "viewport",
        erhe::application::Rendergraph_node_key::viewport
    );

    erhe::message_bus::Message_bus_node<Editor_message>::initialize(
        components.get<Editor_scenes>()->get_editor_message_bus()
    );
}

Viewport_window::~Viewport_window()
{
    if (m_viewport_windows)
    {
        m_viewport_windows->erase(this);
    }
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

void Viewport_window::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION

    const auto& output_viewport = get_producer_output_viewport(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        erhe::application::Rendergraph_node_key::viewport
    );
    const auto& output_framebuffer = get_producer_output_framebuffer(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        erhe::application::Rendergraph_node_key::viewport
    );

    const GLint output_framebuffer_name = output_framebuffer
        ? output_framebuffer->gl_name()
        : 0;

    if (
        (output_viewport.width < 1) ||
        (output_viewport.height < 1)
    )
    {
        return;
    }

    const Render_context context
    {
        .scene_view             = this,
        .viewport_window        = this,
        .viewport_config        = m_viewport_config.get(),
        .camera                 = m_camera.lock().get(),
        .viewport               = output_viewport,
        .override_shader_stages = get_override_shader_stages()
    };

    if (m_is_hovered && m_configuration->id_renderer.enabled)
    {
        m_editor_rendering->render_id(context);
    }

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, output_framebuffer_name);
    if (output_framebuffer)
    {
        if (!output_framebuffer->check_status())
        {
            return;
        }
    }

    m_editor_rendering->render_viewport_main(context, m_is_hovered);
}

void Viewport_window::reconfigure(const int sample_count)
{
    const auto resolve_node = m_multisample_resolve_node.lock();
    if (resolve_node)
    {
        resolve_node->reconfigure(sample_count);
    }
}

void Viewport_window::connect(Viewport_windows* viewport_windows)
{
    m_viewport_windows = viewport_windows;
}

void Viewport_window::set_window_viewport(
    const int x,
    const int y,
    const int width,
    const int height
)
{
    m_window_viewport.x          = x;
    m_window_viewport.y          = y;
    m_window_viewport.width      = width;
    m_window_viewport.height     = height;
    m_projection_viewport.x      = 0;
    m_projection_viewport.y      = 0;
    m_projection_viewport.width  = width;
    m_projection_viewport.height = height;
}

void Viewport_window::set_is_hovered(const bool is_hovered)
{
    m_is_hovered = is_hovered;
}

void Viewport_window::set_camera(const std::shared_ptr<erhe::scene::Camera>& camera)
{
    // TODO Add validation
    m_camera = camera;
}

auto Viewport_window::is_hovered() const -> bool
{
    return m_is_hovered;
}

[[nodiscard]] auto Viewport_window::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root.lock();
}

auto Viewport_window::window_viewport() const -> const erhe::scene::Viewport&
{
    return m_window_viewport;
}

auto Viewport_window::projection_viewport() const -> const erhe::scene::Viewport&
{
    return m_projection_viewport;
}

auto Viewport_window::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_camera.lock();
}

auto Viewport_window::to_scene_content(
    const glm::vec2 position_in_root
) const -> glm::vec2
{
    const float content_x      = static_cast<float>(position_in_root.x) - m_window_viewport.x;
    const float content_y      = static_cast<float>(position_in_root.y) - m_window_viewport.y;
    const float content_flip_y = m_window_viewport.height - content_y;
    return {
        content_x,
        content_flip_y
    };
}

auto Viewport_window::project_to_viewport(
    const glm::dvec3 position_in_world
) const -> std::optional<glm::dvec3>
{
    const auto camera = m_camera.lock();
    if (!camera)
    {
        return {};
    }
    const auto camera_projection_transforms = camera->projection_transforms(m_projection_viewport);
    constexpr double depth_range_near = 0.0;
    constexpr double depth_range_far  = 1.0;
    return erhe::toolkit::project_to_screen_space<double>(
        camera_projection_transforms.clip_from_world.matrix(),
        position_in_world,
        depth_range_near,
        depth_range_far,
        static_cast<double>(m_projection_viewport.x),
        static_cast<double>(m_projection_viewport.y),
        static_cast<double>(m_projection_viewport.width),
        static_cast<double>(m_projection_viewport.height)
    );
}

auto Viewport_window::unproject_to_world(
    const glm::dvec3 position_in_window
) const -> std::optional<glm::dvec3>
{
    const auto camera = m_camera.lock();
    if (!camera)
    {
        return {};
    }
    const auto camera_projection_transforms = camera->projection_transforms(m_projection_viewport);
    constexpr double depth_range_near = 0.0;
    constexpr double depth_range_far  = 1.0;
    return erhe::toolkit::unproject<double>(
        camera_projection_transforms.clip_from_world.inverse_matrix(),
        position_in_window,
        depth_range_near,
        depth_range_far,
        static_cast<double>(m_projection_viewport.x),
        static_cast<double>(m_projection_viewport.y),
        static_cast<double>(m_projection_viewport.width),
        static_cast<double>(m_projection_viewport.height)
    );
}

void Viewport_window::raytrace()
{
    ERHE_PROFILE_FUNCTION

    const auto pointer_near = position_in_world_viewport_depth(1.0);
    const auto pointer_far  = position_in_world_viewport_depth(0.0);

    if (!pointer_near.has_value() || !pointer_far.has_value())
    {
        reset_control_ray();
        return;
    }

    const glm::dvec3 ray_origin   {pointer_near.value()};
    const glm::dvec3 ray_direction{glm::normalize(pointer_far.value() - pointer_near.value())};

    raytrace_update(ray_origin, ray_direction);
}

void Viewport_window::update_grid_hover()
{
    const auto pointer_near = position_in_world_viewport_depth(1.0);
    const auto pointer_far  = position_in_world_viewport_depth(0.0);

    if (!pointer_near.has_value() || !pointer_far.has_value())
    {
        return;
    }

    const glm::dvec3 ray_origin   {pointer_near.value()};
    const glm::dvec3 ray_direction{glm::normalize(pointer_far.value() - pointer_near.value())};

    const Grid_hover_position hover_position = m_grid_tool->update_hover(ray_origin, ray_direction);
    Hover_entry entry;
    entry.valid = (hover_position.grid != nullptr);
    if (entry.valid)
    {
        entry.position = hover_position.position;
        entry.normal   = glm::vec3{
            hover_position.grid->world_from_grid() * glm::dvec4{0.0, 1.0, 0.0, 0.0}
        };
        entry.grid     = hover_position.grid;
    }

    set_hover(Hover_entry::grid_slot, entry);
}

void Viewport_window::update_pointer_context(
    Id_renderer&    id_renderer,
    const glm::vec2 position_in_viewport
)
{
    ERHE_PROFILE_FUNCTION

    const bool reverse_depth = projection_viewport().reverse_depth;
    m_position_in_viewport   = position_in_viewport;
    const auto near_position_in_world = position_in_world_viewport_depth(reverse_depth ? 1.0f : 0.0f);
    const auto far_position_in_world  = position_in_world_viewport_depth(reverse_depth ? 0.0f : 1.0f);
    if (near_position_in_world.has_value() && far_position_in_world.has_value())
    {
        m_control_ray_origin_in_world = near_position_in_world;
        const auto direction = glm::normalize(far_position_in_world.value() - near_position_in_world.value());
        m_control_ray_direction_in_world = direction;
    }
    else
    {
        m_control_ray_origin_in_world.reset();
        m_control_ray_direction_in_world.reset();
    }

    const auto camera = m_camera.lock();
    if (!camera)
    {
        reset_control_ray();
        return;
    }

    if (!m_is_hovered)
    {
        reset_control_ray();
        return;
    }

    if (m_configuration->id_renderer.enabled)
    {
        const auto id_query = id_renderer.get(
            static_cast<int>(position_in_viewport.x),
            static_cast<int>(position_in_viewport.y)
        );
        if (!id_query.valid)
        {
            SPDLOG_LOGGER_TRACE(log_controller_ray, "pointer context hover not valid");
            return;
        }

        Hover_entry entry
        {
            .valid       = id_query.valid,
            .mesh        = id_query.mesh,
            .position    = position_in_world_viewport_depth(id_query.depth),
            .primitive   = id_query.mesh_primitive_index,
            .local_index = id_query.local_index
        };

        SPDLOG_LOGGER_TRACE(log_controller_ray, "position in world = {}", entry.position.value());

        if (entry.mesh)
        {
            const erhe::scene::Node* node = entry.mesh->get_node();
            if (node != nullptr)
            {
                const auto& primitive = entry.mesh->mesh_data.primitives[entry.primitive];
                entry.geometry = primitive.source_geometry;
                if (entry.geometry != nullptr)
                {
                    const auto polygon_id = static_cast<erhe::geometry::Polygon_id>(entry.local_index);
                    if (polygon_id < entry.geometry->get_polygon_count())
                    {
                        SPDLOG_LOGGER_TRACE(log_controller_ray, "hover polygon = {}", polygon_id);
                        auto* const polygon_normals = entry.geometry->polygon_attributes().find<glm::vec3>(
                            erhe::geometry::c_polygon_normals
                        );
                        if (
                            (polygon_normals != nullptr) &&
                            polygon_normals->has(polygon_id)
                        )
                        {
                            const auto local_normal    = polygon_normals->get(polygon_id);
                            const auto world_from_node = node->world_from_node();
                            entry.normal = glm::vec3{world_from_node * glm::vec4{local_normal, 0.0f}};
                            SPDLOG_LOGGER_TRACE(log_controller_ray, "hover normal = {}", entry.normal.value());
                        }
                    }
                }
            }
        }

        using erhe::toolkit::test_all_rhs_bits_set;

        const uint64_t flags = id_query.mesh ? entry.mesh->get_flag_bits() : 0;

        const bool hover_content      = id_query.mesh && test_all_rhs_bits_set(flags, erhe::scene::Scene_item_flags::content     );
        const bool hover_tool         = id_query.mesh && test_all_rhs_bits_set(flags, erhe::scene::Scene_item_flags::tool        );
        const bool hover_brush        = id_query.mesh && test_all_rhs_bits_set(flags, erhe::scene::Scene_item_flags::brush       );
        const bool hover_rendertarget = id_query.mesh && test_all_rhs_bits_set(flags, erhe::scene::Scene_item_flags::rendertarget);
        SPDLOG_LOGGER_TRACE(
            log_controller_ray,
            "hover mesh = {} primitive = {} local index {} {}{}{}{}",
            entry.mesh ? entry.mesh->name() : "()",
            entry.primitive,
            entry.local_index,
            hover_content      ? "content "      : "",
            hover_tool         ? "tool "         : "",
            hover_brush        ? "brush "        : "",
            hover_rendertarget ? "rendertarget " : ""
        );
        set_hover(Hover_entry::content_slot     , hover_content      ? entry : Hover_entry{});
        set_hover(Hover_entry::tool_slot        , hover_tool         ? entry : Hover_entry{});
        set_hover(Hover_entry::brush_slot       , hover_brush        ? entry : Hover_entry{});
        set_hover(Hover_entry::rendertarget_slot, hover_rendertarget ? entry : Hover_entry{});

        const auto scene_root = m_scene_root.lock();
        if (scene_root)
        {
            scene_root->update_pointer_for_rendertarget_meshes();
        }
    }
    else
    {
        raytrace();
    }

    update_grid_hover();
}

auto Viewport_window::position_in_world_viewport_depth(
    const double viewport_depth
) const -> std::optional<glm::dvec3>
{
    const auto camera = m_camera.lock();
    if (
        !m_position_in_viewport.has_value() ||
        !camera
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
    const auto      vp                    = projection_viewport();
    const auto      projection_transforms = camera->projection_transforms(vp);
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

auto Viewport_window::get_shadow_render_node() const -> Shadow_render_node*
{
    const std::weak_ptr<Rendergraph_node>& input_node_weak = get_consumer_input_node(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        erhe::application::Rendergraph_node_key::shadow_maps
    );
    const std::shared_ptr<Rendergraph_node>& input_node_shared = input_node_weak.lock();
    Rendergraph_node*   input_node = input_node_shared.get();
    Shadow_render_node* shadow_render_node = reinterpret_cast<Shadow_render_node*>(input_node);
    return shadow_render_node;
}

void Viewport_window::imgui_toolbar()
{
    if (m_trs_tool)
    {
        m_trs_tool->viewport_toolbar();
    }

    if (m_grid_tool)
    {
        m_grid_tool->viewport_toolbar();
    }

    if (m_physics_window)
    {
        m_physics_window->viewport_toolbar();
    }

    const float  rounding        {3.0f};
    const ImVec4 background_color{0.20f, 0.26f, 0.25f, 0.72f};

    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    erhe::application::make_text_with_background("Camera:", rounding, background_color);
    ImGui::SameLine();
    get_scene_root()->camera_combo("##Camera", m_camera);
    ImGui::SameLine();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    erhe::application::make_text_with_background("Shader:", rounding, background_color);
    ImGui::SameLine();
    ImGui::Combo(
        "##Shader",
        reinterpret_cast<int*>(&m_shader_stages_variant),
        c_shader_stages_variant_strings,
        IM_ARRAYSIZE(c_shader_stages_variant_strings),
        IM_ARRAYSIZE(c_shader_stages_variant_strings)
    );
    ImGui::SameLine();

    const auto& post_processing_node = m_post_processing_node.lock();
    if (post_processing_node)
    {
        ImGui::SameLine();
        post_processing_node->imgui_toolbar();
    }
}

void Viewport_window::link_to(std::weak_ptr<erhe::application::Multisample_resolve_node> node)
{
    m_multisample_resolve_node = node;
}

void Viewport_window::link_to(std::weak_ptr<Post_processing_node> node)
{
    m_post_processing_node = node;
}

auto Viewport_window::get_post_processing_node() -> std::shared_ptr<Post_processing_node>
{
    return m_post_processing_node.lock();
}

void Viewport_window::set_final_output(
    std::weak_ptr<erhe::application::Rendergraph_node> node
)
{
    m_final_output = node;
}

auto Viewport_window::get_final_output() -> std::weak_ptr<erhe::application::Rendergraph_node>
{
    return m_final_output;
}

} // namespace editor
