// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/viewport_window.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "rendergraph/post_processing.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/grid_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "tools/transform/transform_tool.hpp"
#include "windows/viewport_config_window.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/multisample_resolve.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_renderer/line_renderer.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
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
    Editor_context&                             editor_context,
    erhe::rendergraph::Rendergraph&             rendergraph,
    Tools&                                      tools,
    Viewport_config_window&                     viewport_config_window,
    const std::string_view                      name,
    const char*                                 ini_label,
    const std::shared_ptr<Scene_root>&          scene_root,
    const std::shared_ptr<erhe::scene::Camera>& camera
)
    : Scene_view       {editor_context, viewport_config_window.config}
    , Rendergraph_node {rendergraph, name}
    , m_name           {name}
    , m_ini_label      {ini_label}
    , m_scene_root     {scene_root}
    , m_tool_scene_root{tools.get_tool_scene_root()}
    , m_camera         {camera}
{
    register_input(
        erhe::rendergraph::Resource_routing::Resource_provided_by_producer,
        "shadow_maps",
        erhe::rendergraph::Rendergraph_node_key::shadow_maps
    );
    register_input(
        erhe::rendergraph::Resource_routing::Resource_provided_by_producer,
        "rendertarget texture",
        erhe::rendergraph::Rendergraph_node_key::rendertarget_texture
    );
    register_output(
        erhe::rendergraph::Resource_routing::Resource_provided_by_consumer,
        "viewport",
        erhe::rendergraph::Rendergraph_node_key::viewport
    );
}

Viewport_window::~Viewport_window() noexcept
{
    m_context.viewport_windows->erase(this);
}

auto Viewport_window::get_override_shader_stages() const -> erhe::graphics::Shader_stages*
{
    auto& programs = *m_context.programs;
    switch (m_shader_stages_variant) {
        case Shader_stages_variant::error:                    return &programs.error.shader_stages;
        case Shader_stages_variant::standard:                 return &programs.standard.shader_stages;
        case Shader_stages_variant::anisotropic_slope:        return &programs.anisotropic_slope.shader_stages;
        case Shader_stages_variant::anisotropic_engine_ready: return &programs.anisotropic_engine_ready.shader_stages;
        case Shader_stages_variant::circular_brushed_metal:   return &programs.circular_brushed_metal.shader_stages;
        case Shader_stages_variant::debug_depth:              return &programs.debug_depth.shader_stages;
        case Shader_stages_variant::debug_normal:             return &programs.debug_normal.shader_stages;
        case Shader_stages_variant::debug_tangent:            return &programs.debug_tangent.shader_stages;
        case Shader_stages_variant::debug_bitangent:          return &programs.debug_bitangent.shader_stages;
        case Shader_stages_variant::debug_texcoord:           return &programs.debug_texcoord.shader_stages;
        case Shader_stages_variant::debug_base_color_texture: return &programs.debug_base_color_texture.shader_stages;
        case Shader_stages_variant::debug_vertex_color_rgb:   return &programs.debug_vertex_color_rgb.shader_stages;
        case Shader_stages_variant::debug_vertex_color_alpha: return &programs.debug_vertex_color_alpha.shader_stages;
        case Shader_stages_variant::debug_aniso_strength:     return &programs.debug_aniso_strength.shader_stages;
        case Shader_stages_variant::debug_aniso_texcoord:     return &programs.debug_aniso_texcoord.shader_stages;
        case Shader_stages_variant::debug_vdotn:              return &programs.debug_vdotn.shader_stages;
        case Shader_stages_variant::debug_ldotn:              return &programs.debug_ldotn.shader_stages;
        case Shader_stages_variant::debug_hdotv:              return &programs.debug_hdotv.shader_stages;
        case Shader_stages_variant::debug_joint_indices:      return &programs.debug_joint_indices.shader_stages;
        case Shader_stages_variant::debug_joint_weights:      return &programs.debug_joint_weights.shader_stages;
        case Shader_stages_variant::debug_omega_o:            return &programs.debug_omega_o.shader_stages;
        case Shader_stages_variant::debug_omega_i:            return &programs.debug_omega_i.shader_stages;
        case Shader_stages_variant::debug_omega_g:            return &programs.debug_omega_g.shader_stages;
        case Shader_stages_variant::debug_misc:               return &programs.debug_misc.shader_stages;
        default:                                              return &programs.error.shader_stages;
    }
}

void Viewport_window::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

    const auto& output_viewport = get_producer_output_viewport(
        erhe::rendergraph::Resource_routing::Resource_provided_by_consumer,
        erhe::rendergraph::Rendergraph_node_key::viewport
    );
    const auto& output_framebuffer = get_producer_output_framebuffer(
        erhe::rendergraph::Resource_routing::Resource_provided_by_consumer,
        erhe::rendergraph::Rendergraph_node_key::viewport
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

    m_context.editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_render_scene_view,
            .scene_view   = this
        }
    );

    const Render_context context
    {
        .editor_context         = m_context,
        .scene_view             = *this,
        .viewport_config        = m_viewport_config,
        .camera                 = *m_camera.lock().get(),
        .viewport_window        = this,
        .viewport               = output_viewport,
        .override_shader_stages = get_override_shader_stages()
    };

    if (m_is_hovered && m_context.id_renderer->enabled) {
        m_context.editor_rendering->render_id(context);
    }

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, output_framebuffer_name);
    if (output_framebuffer) {
        if (!output_framebuffer->check_status()) {
            return;
        }
    }

    gl::enable(gl::Enable_cap::scissor_test);
    gl::scissor(context.viewport.x, context.viewport.y, context.viewport.width, context.viewport.height);

    auto scene_root = m_scene_root.lock();
    if (!scene_root || m_camera.expired()) {
        gl::clear_color(0.1f, 0.1f, 0.1f, 1.0f);
        gl::clear      (gl::Clear_buffer_mask::color_buffer_bit);
        gl::disable    (gl::Enable_cap::scissor_test);
        return;
    }

    m_context.editor_rendering->render_viewport_main(context);

    m_context.line_renderer_set->begin();
    m_context.tools            ->render_viewport_tools(context);
    m_context.editor_rendering ->render_viewport_renderables(context);
    m_context.line_renderer_set->end();
    m_context.line_renderer_set->render(context.viewport, context.camera);

    m_context.text_renderer->render(context.viewport);
    gl::disable(gl::Enable_cap::scissor_test);
}

void Viewport_window::reconfigure(const int sample_count)
{
    if (m_multisample_resolve_node != nullptr) {
        m_multisample_resolve_node->reconfigure(sample_count);
    }
}

void Viewport_window::set_window_viewport(
    erhe::math::Viewport viewport
)
{
    m_window_viewport = viewport;
    m_projection_viewport.x      = 0;
    m_projection_viewport.y      = 0;
    m_projection_viewport.width  = viewport.width;
    m_projection_viewport.height = viewport.height;
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

auto Viewport_window::window_viewport() const -> const erhe::math::Viewport&
{
    return m_window_viewport;
}

auto Viewport_window::projection_viewport() const -> const erhe::math::Viewport&
{
    return m_projection_viewport;
}

auto Viewport_window::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_camera.lock();
}

auto Viewport_window::get_rendergraph_node() -> erhe::rendergraph::Rendergraph_node*
{
    return this;
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
    return erhe::math::project_to_screen_space<float>(
        camera_projection_transforms.clip_from_world.get_matrix(),
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
    return erhe::math::unproject<float>(
        camera_projection_transforms.clip_from_world.get_inverse_matrix(),
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

    if (m_context.id_renderer->enabled) {
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
    const auto id_query = m_context.id_renderer->get(
        static_cast<int>(position_in_viewport.x),
        static_cast<int>(position_in_viewport.y)
    );
    if (!id_query.valid) {
        SPDLOG_LOGGER_TRACE(log_controller_ray, "pointer context hover not valid");
        return;
    }

    Hover_entry entry{
        .valid           = id_query.valid,
        .mesh            = id_query.mesh,
        .primitive_index = id_query.primitive_index,
        .position        = position_in_world_viewport_depth(id_query.depth),
        .triangle_id     = id_query.triangle_id
    };

    SPDLOG_LOGGER_TRACE(log_controller_ray, "position in world = {}", entry.position.value());

    if (entry.mesh != nullptr) {
        const erhe::scene::Node* node = entry.mesh->get_node();
        ERHE_VERIFY(node != nullptr);
        const auto& primitive          = entry.mesh->get_primitives()[entry.primitive_index];
        const auto& geometry_primitive = primitive.geometry_primitive;
        ERHE_VERIFY(geometry_primitive);
        entry.geometry = geometry_primitive->source_geometry;
        if (entry.geometry) {
            const auto triangle_id = static_cast<erhe::geometry::Polygon_id>(entry.triangle_id);
            const auto polygon_id  = geometry_primitive->gl_geometry_mesh.primitive_id_to_polygon_id[triangle_id];
            ERHE_VERIFY(polygon_id < entry.geometry->get_polygon_count());
            SPDLOG_LOGGER_TRACE(log_controller_ray, "hover polygon = {}", polygon_id);
            auto* const polygon_normals = entry.geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals);
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

    using namespace erhe::bit;

    const uint64_t flags = (id_query.mesh != nullptr) ? entry.mesh->get_flag_bits() : 0;

    const bool hover_content      = id_query.mesh && test_all_rhs_bits_set(flags, erhe::Item_flags::content     );
    const bool hover_tool         = id_query.mesh && test_all_rhs_bits_set(flags, erhe::Item_flags::tool        );
    const bool hover_brush        = id_query.mesh && test_all_rhs_bits_set(flags, erhe::Item_flags::brush       );
    const bool hover_rendertarget = id_query.mesh && test_all_rhs_bits_set(flags, erhe::Item_flags::rendertarget);
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
    const glm::mat4 world_from_clip       = projection_transforms.clip_from_world.get_inverse_matrix();

    return erhe::math::unproject<float>(
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
    Rendergraph_node* input_node = get_consumer_input_node(
        erhe::rendergraph::Resource_routing::Resource_provided_by_producer,
        erhe::rendergraph::Rendergraph_node_key::shadow_maps
    );
    Shadow_render_node* shadow_render_node = static_cast<Shadow_render_node*>(input_node);
    return shadow_render_node;
}

auto Viewport_window::viewport_toolbar() -> bool
{
    bool hovered = false;
    m_context.viewport_windows->viewport_toolbar(*this, hovered);

    //// TODO Tool_flags::viewport_toolbar
    m_context.selection_tool->viewport_toolbar(hovered);
    m_context.transform_tool->viewport_toolbar(hovered);
    m_context.grid_tool->viewport_toolbar(hovered);
    //// TODO m_physics_window.viewport_toolbar(hovered);

    const float  rounding        {3.0f};
    const ImVec4 background_color{0.20f, 0.26f, 0.25f, 0.72f};

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    erhe::imgui::make_text_with_background("Scene:", rounding, background_color);
    if (ImGui::IsItemHovered()) {
        hovered = true;
    }
    ImGui::SameLine();
    auto                        old_scene_root = m_scene_root;
    std::shared_ptr<Scene_root> scene_root     = get_scene_root();
    Scene_root*                 scene_root_raw = scene_root.get();
    ImGui::SetNextItemWidth(110.0f);
    const bool combo_used     = m_context.editor_scenes->scene_combo("##Scene", scene_root_raw, false);
    if (ImGui::IsItemHovered()) {
        hovered = true;
    }
    if (combo_used) {
        scene_root = (scene_root_raw != nullptr) 
            ? scene_root_raw->shared_from_this()
            : std::shared_ptr<Scene_root>{};
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
    if (!scene_root) {
        return hovered;
    }

    //get_scene_root()->camera_combo("##Camera", m_camera);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    erhe::imgui::make_text_with_background("Camera:", rounding, background_color);
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
    erhe::imgui::make_text_with_background("Shader:", rounding, background_color);
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

void Viewport_window::link_to(std::shared_ptr<erhe::rendergraph::Multisample_resolve_node> node)
{
    m_multisample_resolve_node = node;
}

void Viewport_window::link_to(std::shared_ptr<Post_processing_node> node)
{
    m_post_processing_node = node;
}

auto Viewport_window::get_post_processing_node() -> Post_processing_node*
{
    return m_post_processing_node.get();
}

void Viewport_window::set_final_output(
    std::shared_ptr<erhe::rendergraph::Rendergraph_node> node
)
{
    m_final_output = node;
}

auto Viewport_window::get_final_output() -> erhe::rendergraph::Rendergraph_node*
{
    return m_final_output.get();
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
    const auto ss_closest = erhe::math::closest_point<float>(
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
            const auto closest_points_r = erhe::math::closest_points<float>(P0, P1, R0, R1);
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
            const auto closest_points_q = erhe::math::closest_points<float>(P0, P1, Q0, Q1);
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

    const auto intersection = erhe::math::intersect_plane<float>(N, P, Q0, v);
    if (!intersection.has_value()) {
        return {};
    }

    const vec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    return drag_point_new_position_in_world;
}

} // namespace editor
