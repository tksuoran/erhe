// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/viewport_window.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "rendergraph/post_processing.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/grid.hpp"
#include "tools/grid_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "tools/transform/transform_tool.hpp"
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
#include "erhe/toolkit/math_util.hpp"
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

Viewport_window::Viewport_window(
    const std::string_view                      name,
    const std::shared_ptr<Scene_root>&          scene_root,
    const std::shared_ptr<erhe::scene::Camera>& camera
)
    : erhe::application::Rendergraph_node{name}
    , m_viewport_config                  {g_viewport_config_window->config}
    , m_name                             {name}
    , m_scene_root                       {scene_root}
    , m_tool_scene_root                  {g_tools->get_tool_scene_root()}
    , m_camera                           {camera}
{
    register_input(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "shadow_maps",
        erhe::application::Rendergraph_node_key::shadow_maps
    );
    register_input(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "rendertarget texture",
        erhe::application::Rendergraph_node_key::rendertarget_texture
    );
    register_output(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        "viewport",
        erhe::application::Rendergraph_node_key::viewport
    );
}

Viewport_window::~Viewport_window()
{
    if (g_viewport_windows != nullptr) {
        g_viewport_windows->erase(this);
    }
}

auto Viewport_window::get_override_shader_stages() const -> erhe::graphics::Shader_stages*
{
    switch (m_shader_stages_variant) {
        case Shader_stages_variant::standard:                 return g_programs->standard.get();
        case Shader_stages_variant::anisotropic_slope:        return g_programs->anisotropic_slope.get();
        case Shader_stages_variant::anisotropic_engine_ready: return g_programs->anisotropic_engine_ready.get();
        case Shader_stages_variant::circular_brushed_metal:   return g_programs->circular_brushed_metal.get();
        case Shader_stages_variant::debug_depth:              return g_programs->debug_depth.get();
        case Shader_stages_variant::debug_normal:             return g_programs->debug_normal.get();
        case Shader_stages_variant::debug_tangent:            return g_programs->debug_tangent.get();
        case Shader_stages_variant::debug_bitangent:          return g_programs->debug_bitangent.get();
        case Shader_stages_variant::debug_texcoord:           return g_programs->debug_texcoord.get();
        case Shader_stages_variant::debug_vertex_color_rgb:   return g_programs->debug_vertex_color_rgb.get();
        case Shader_stages_variant::debug_vertex_color_alpha: return g_programs->debug_vertex_color_alpha.get();
        case Shader_stages_variant::debug_omega_o:            return g_programs->debug_omega_o.get();
        case Shader_stages_variant::debug_omega_i:            return g_programs->debug_omega_i.get();
        case Shader_stages_variant::debug_omega_g:            return g_programs->debug_omega_g.get();
        case Shader_stages_variant::debug_misc:               return g_programs->debug_misc.get();
        default:                                              return g_programs->standard.get();
    }
}

void Viewport_window::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

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
    ) {
        return;
    }

    auto scene_root = m_scene_root.lock();
    if (!scene_root) {
        return;
    }

    g_editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_render_scene_view,
            .scene_view   = this
        }
    );

    const Render_context context
    {
        .scene_view             = this,
        .viewport_window        = this,
        .viewport_config        = &m_viewport_config,
        .camera                 = m_camera.lock().get(),
        .viewport               = output_viewport,
        .override_shader_stages = get_override_shader_stages()
    };

    if (m_is_hovered && g_id_renderer->config.enabled) {
        g_editor_rendering->render_id(context);
    }

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, output_framebuffer_name);
    if (output_framebuffer) {
        if (!output_framebuffer->check_status()) {
            return;
        }
    }

    g_editor_rendering->render_viewport_main(context, m_is_hovered);
}

void Viewport_window::reconfigure(const int sample_count)
{
    const auto resolve_node = m_multisample_resolve_node.lock();
    if (resolve_node) {
        resolve_node->reconfigure(sample_count);
    }
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

auto Viewport_window::get_rendergraph_node() -> std::shared_ptr<erhe::application::Rendergraph_node>
{
    return shared_from_this();
}

auto Viewport_window::as_viewport_window() -> Viewport_window*
{
    return this;
}

auto Viewport_window::as_viewport_window() const -> const Viewport_window*
{
    return this;
}

auto Viewport_window::viewport_from_window(
    const glm::vec2 window_position
) const -> glm::vec2
{
    const float content_x      = static_cast<float>(window_position.x) - m_window_viewport.x;
    const float content_y      = static_cast<float>(window_position.y) - m_window_viewport.y;
    const float content_flip_y = m_window_viewport.height - content_y;
    return glm::vec2{content_x, content_flip_y};
}

auto Viewport_window::project_to_viewport(
    const glm::vec3 position_in_world
) const -> std::optional<glm::vec3>
{
    const auto camera = m_camera.lock();
    if (!camera) {
        return {};
    }
    const auto camera_projection_transforms = camera->projection_transforms(m_projection_viewport);
    constexpr float depth_range_near = 0.0f;
    constexpr float depth_range_far  = 1.0f;
    return erhe::toolkit::project_to_screen_space<float>(
        camera_projection_transforms.clip_from_world.matrix(),
        position_in_world,
        depth_range_near,
        depth_range_far,
        static_cast<float>(m_projection_viewport.x),
        static_cast<float>(m_projection_viewport.y),
        static_cast<float>(m_projection_viewport.width),
        static_cast<float>(m_projection_viewport.height)
    );
}

auto Viewport_window::unproject_to_world(
    const glm::vec3 position_in_window
) const -> std::optional<glm::vec3>
{
    const auto camera = m_camera.lock();
    if (!camera) {
        return {};
    }
    const auto camera_projection_transforms = camera->projection_transforms(m_projection_viewport);
    constexpr float depth_range_near = 0.0f;
    constexpr float depth_range_far  = 1.0f;
    return erhe::toolkit::unproject<float>(
        camera_projection_transforms.clip_from_world.inverse_matrix(),
        position_in_window,
        depth_range_near,
        depth_range_far,
        static_cast<float>(m_projection_viewport.x),
        static_cast<float>(m_projection_viewport.y),
        static_cast<float>(m_projection_viewport.width),
        static_cast<float>(m_projection_viewport.height)
    );
}

void Viewport_window::update_pointer_2d_position(
    const glm::vec2 position_in_viewport
)
{
    ERHE_PROFILE_FUNCTION();

    m_position_in_viewport = position_in_viewport;
}

void Viewport_window::update_hover()
{
    const bool reverse_depth          = projection_viewport().reverse_depth;
    const auto near_position_in_world = position_in_world_viewport_depth(reverse_depth ? 1.0f : 0.0f);
    const auto far_position_in_world  = position_in_world_viewport_depth(reverse_depth ? 0.0f : 1.0f);

    const auto camera = m_camera.lock();
    if (
        !near_position_in_world.has_value() ||
        !far_position_in_world.has_value() ||
        !camera ||
        !m_is_hovered
    ) {
        reset_control_transform();
        reset_hover_slots();
        return;
    }

    set_world_from_control(
        near_position_in_world.value(),
        far_position_in_world.value()
    );

    const auto scene_root = m_scene_root.lock();
    if (scene_root) {
        scene_root->update_pointer_for_rendertarget_meshes(this);
    }

    if (g_id_renderer->config.enabled) {
        update_hover_with_id_render();
    } else {
        update_hover_with_raytrace();
    }

    update_grid_hover();

}

void Viewport_window::update_hover_with_id_render()
{
    if (!m_position_in_viewport.has_value()) {
        reset_hover_slots();
        return;
    }
    const auto position_in_viewport = m_position_in_viewport.value();
    const auto id_query = g_id_renderer->get(
        static_cast<int>(position_in_viewport.x),
        static_cast<int>(position_in_viewport.y)
    );
    if (!id_query.valid) {
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

    if (entry.mesh) {
        const erhe::scene::Node* node = entry.mesh->get_node();
        if (node != nullptr) {
            const auto& primitive = entry.mesh->mesh_data.primitives[entry.primitive];
            entry.geometry = primitive.source_geometry;
            if (entry.geometry != nullptr) {
                const auto polygon_id = static_cast<erhe::geometry::Polygon_id>(entry.local_index);
                if (polygon_id < entry.geometry->get_polygon_count()) {
                    SPDLOG_LOGGER_TRACE(log_controller_ray, "hover polygon = {}", polygon_id);
                    auto* const polygon_normals = entry.geometry->polygon_attributes().find<glm::vec3>(
                        erhe::geometry::c_polygon_normals
                    );
                    if (
                        (polygon_normals != nullptr) &&
                        polygon_normals->has(polygon_id)
                    ) {
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

    const bool hover_content      = id_query.mesh && test_all_rhs_bits_set(flags, erhe::scene::Item_flags::content     );
    const bool hover_tool         = id_query.mesh && test_all_rhs_bits_set(flags, erhe::scene::Item_flags::tool        );
    const bool hover_brush        = id_query.mesh && test_all_rhs_bits_set(flags, erhe::scene::Item_flags::brush       );
    const bool hover_rendertarget = id_query.mesh && test_all_rhs_bits_set(flags, erhe::scene::Item_flags::rendertarget);
    SPDLOG_LOGGER_TRACE(
        log_frame,
        "hover mesh = {} primitive = {} local index {} {}{}{}{}",
        entry.mesh ? entry.mesh->get_name() : "()",
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
}

auto Viewport_window::get_position_in_viewport() const -> std::optional<glm::vec2>
{
    return m_position_in_viewport;
}

auto Viewport_window::position_in_world_viewport_depth(
    const float viewport_depth
) const -> std::optional<glm::vec3>
{
    const auto camera = m_camera.lock();
    if (
        !m_position_in_viewport.has_value() ||
        !camera
    ) {
        return {};
    }

    const float depth_range_near     = 0.0f;
    const float depth_range_far      = 1.0f;
    const auto  position_in_viewport = glm::vec3{
        m_position_in_viewport.value().x,
        m_position_in_viewport.value().y,
        viewport_depth
    };
    const auto      vp                    = projection_viewport();
    const auto      projection_transforms = camera->projection_transforms(vp);
    const glm::mat4 world_from_clip       = projection_transforms.clip_from_world.inverse_matrix();

    return erhe::toolkit::unproject<float>(
        glm::mat4{world_from_clip},
        position_in_viewport,
        depth_range_near,
        depth_range_far,
        static_cast<float>(vp.x),
        static_cast<float>(vp.y),
        static_cast<float>(vp.width),
        static_cast<float>(vp.height)
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

auto Viewport_window::get_config() -> Viewport_config*
{
    return &m_viewport_config;
}

auto Viewport_window::viewport_toolbar() -> bool
{
    bool hovered = false;
    if (g_viewport_windows != nullptr) {
        g_viewport_windows->viewport_toolbar(*this, hovered);
    }
    //// TODO Tool_flags::viewport_toolbar
    if (g_selection_tool != nullptr) {
        g_selection_tool->viewport_toolbar(hovered);
    }
    if (g_transform_tool != nullptr) {
        g_transform_tool->viewport_toolbar(hovered);
    }

    if (g_grid_tool != nullptr) {
        g_grid_tool->viewport_toolbar(hovered);
    }

    if (g_physics_window != nullptr) {
        g_physics_window->viewport_toolbar(hovered);
    }

    const float  rounding        {3.0f};
    const ImVec4 background_color{0.20f, 0.26f, 0.25f, 0.72f};

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    erhe::application::make_text_with_background("Scene:", rounding, background_color);
    if (ImGui::IsItemHovered()) {
        hovered = true;
    }
    ImGui::SameLine();
    auto       old_scene_root = m_scene_root;
    auto       scene_root     = get_scene_root();
    ImGui::SetNextItemWidth(110.0f);
    const bool combo_used     = g_editor_scenes->scene_combo("##Scene", scene_root, false);
    if (ImGui::IsItemHovered()) {
        hovered = true;
    }
    if (combo_used) {
        m_scene_root = scene_root;
        if (old_scene_root.lock() != scene_root) {
            if (scene_root) {
                const auto& cameras = scene_root->get_hosted_scene()->get_cameras();
                m_camera = cameras.empty() ? std::weak_ptr<erhe::scene::Camera>{} : cameras.front();
            } else {
                m_camera.reset();
            }
        }
    }
    scene_root = get_scene_root();
    if (!scene_root) {
        return hovered;
    }

    //get_scene_root()->camera_combo("##Camera", m_camera);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    erhe::application::make_text_with_background("Camera:", rounding, background_color);
    if (ImGui::IsItemHovered()) {
        hovered = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    get_scene_root()->camera_combo("##Camera", m_camera);
    if (ImGui::IsItemHovered()) {
        hovered = true;
    }

    ImGui::SameLine();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    erhe::application::make_text_with_background("Shader:", rounding, background_color);
    if (ImGui::IsItemHovered()) {
        hovered = true;
    }
    ImGui::SameLine();
    ImGui::Combo(
        "##Shader",
        reinterpret_cast<int*>(&m_shader_stages_variant),
        c_shader_stages_variant_strings,
        IM_ARRAYSIZE(c_shader_stages_variant_strings),
        IM_ARRAYSIZE(c_shader_stages_variant_strings)
    );
    if (ImGui::IsItemHovered()) {
        hovered = true;
    }

    //// const auto& post_processing_node = m_post_processing_node.lock();
    //// if (post_processing_node)
    //// {
    ////     ImGui::SameLine();
    ////     post_processing_node->viewport_toolbar();
    //// }
    return hovered;
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

auto Viewport_window::get_closest_point_on_line(
    const glm::vec3 P0,
    const glm::vec3 P1
) -> std::optional<glm::vec3>
{
    ERHE_PROFILE_FUNCTION();

    using vec2 = glm::vec2;
    using vec3 = glm::vec3;

    const auto position_in_viewport_opt = get_position_in_viewport();
    if (!position_in_viewport_opt.has_value()) {
        return {};
    }

    const auto ss_P0_opt = project_to_viewport(P0);
    const auto ss_P1_opt = project_to_viewport(P1);
    if (
        !ss_P0_opt.has_value() ||
        !ss_P1_opt.has_value()
    ) {
        return {};
    }

    const vec3 ss_P0      = ss_P0_opt.value();
    const vec3 ss_P1      = ss_P1_opt.value();
    const auto ss_closest = erhe::toolkit::closest_point<float>(
        vec2{ss_P0},
        vec2{ss_P1},
        vec2{position_in_viewport_opt.value()}
    );

    if (ss_closest.has_value()) {
        const auto R0_opt = unproject_to_world(vec3{ss_closest.value(), 0.0f});
        const auto R1_opt = unproject_to_world(vec3{ss_closest.value(), 1.0f});
        if (R0_opt.has_value() && R1_opt.has_value()) {
            const auto R0 = R0_opt.value();
            const auto R1 = R1_opt.value();
            const auto closest_points_r = erhe::toolkit::closest_points<float>(P0, P1, R0, R1);
            if (closest_points_r.has_value()) {
                return closest_points_r.value().P;
            }
        }
    } else {
        const auto Q0_opt = position_in_world_viewport_depth(1.0);
        const auto Q1_opt = position_in_world_viewport_depth(0.0);
        if (Q0_opt.has_value() && Q1_opt.has_value()) {
            const auto Q0 = Q0_opt.value();
            const auto Q1 = Q1_opt.value();
            const auto closest_points_q = erhe::toolkit::closest_points<float>(P0, P1, Q0, Q1);
            if (closest_points_q.has_value()) {
                return closest_points_q.value().P;
            }
        }
    }

    return {};
}

auto Viewport_window::get_closest_point_on_plane(
    const glm::vec3 N,
    const glm::vec3 P
) -> std::optional<glm::vec3>
{
    ERHE_PROFILE_FUNCTION();

    using vec3 = glm::vec3;

    const auto Q0_opt = position_in_world_viewport_depth(1.0);
    const auto Q1_opt = position_in_world_viewport_depth(0.0);
    if (
        !Q0_opt.has_value() ||
        !Q1_opt.has_value()
    ) {
        return {};
    }

    const vec3 Q0 = Q0_opt.value();
    const vec3 Q1 = Q1_opt.value();
    const vec3 v  = normalize(Q1 - Q0);

    const auto intersection = erhe::toolkit::intersect_plane<float>(N, P, Q0, v);
    if (!intersection.has_value()) {
        return {};
    }

    const vec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    return drag_point_new_position_in_world;
}

} // namespace editor
