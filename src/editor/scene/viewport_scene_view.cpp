// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/viewport_scene_view.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "app_message_bus.hpp"
#include "app_rendering.hpp"
#include "app_scenes.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"

#include <glm/gtx/matrix_operation.hpp>

#include <imgui/imgui.h>

namespace editor {

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;
using erhe::graphics::Render_pass;
using erhe::graphics::Renderbuffer;
using erhe::graphics::Texture;

int Viewport_scene_view::s_serial = 0;

Viewport_scene_view::Viewport_scene_view(
    App_context&                                context,
    erhe::rendergraph::Rendergraph&             rendergraph,
    Tools&                                      tools,
    const std::string_view                      name,
    const char*                                 ini_label,
    const std::shared_ptr<Scene_root>&          scene_root,
    const std::shared_ptr<erhe::scene::Camera>& camera,
    int                                         msaa_sample_count
)
    : Scene_view              {context, Viewport_config::default_config()}
    , Texture_rendergraph_node{
        erhe::rendergraph::Texture_rendergraph_node_create_info{
            .rendergraph          = rendergraph,
            .name                 = fmt::format("Texture_rendergraph_node for Viewport_scene_view {}", name),
            .input_key            = erhe::rendergraph::Rendergraph_node_key::none,
            .output_key           = erhe::rendergraph::Rendergraph_node_key::viewport_texture,
            .color_format         = erhe::dataformat::Format::format_16_vec4_float,
            .depth_stencil_format = erhe::dataformat::Format::format_d32_sfloat_s8_uint,
            .sample_count         = msaa_sample_count
        }
    }
    , m_name                  {name}
    , m_ini_label             {ini_label}
    , m_tool_scene_root       {tools.get_tool_scene_root()}
    , m_camera                {camera}
{
    set_scene_root(scene_root);

    // We need shadows rendered before
    register_input("shadow_maps", erhe::rendergraph::Rendergraph_node_key::shadow_maps);

    // We need rendertarget texture(s) rendered before
    register_input("rendertarget texture", erhe::rendergraph::Rendergraph_node_key::rendertarget_texture);
}

Viewport_scene_view::~Viewport_scene_view() noexcept
{
    m_context.scene_views->erase(this);
}

void Viewport_scene_view::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

    if ((m_projection_viewport.width < 1) || (m_projection_viewport.height < 1)) {
        return;
    }

    bool do_render = true;
    const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
    if (!scene_root) {
        do_render = false;
    }
    std::shared_ptr<erhe::scene::Camera> camera = m_camera.lock();
    if (!camera) {
        do_render = false;
    }

    Render_context context{
        .encoder                = nullptr, // filled in later once we start render pass
        .app_context            = m_context,
        .scene_view             = *this,
        .viewport_config        = m_viewport_config,
        .camera                 = camera.get(),
        .viewport_scene_view    = this,
        .viewport               = m_projection_viewport,
        .override_shader_stages = get_override_shader_stages()
    };

    if (do_render && m_is_scene_view_hovered && m_context.id_renderer->enabled) {
        m_context.app_rendering->render_id(context);
    }

    erhe::graphics::Device& graphics_device = m_rendergraph.get_graphics_device();

    if (do_render) {
        m_context.debug_renderer->begin_frame(context.viewport, *context.camera);

        m_context.tools         ->render_viewport_tools(context);
        m_context.app_rendering ->render_viewport_renderables(context);

        erhe::graphics::Compute_command_encoder compute_encoder = graphics_device.make_compute_command_encoder();
        m_context.debug_renderer->compute(compute_encoder);
    }

    update_render_pass(m_projection_viewport.width, m_projection_viewport.height, nullptr);
    // TODO If we ever have non-ImGui viewport, this might be an option:
    // update_render_pass(m_projection_viewport.width, m_projection_viewport.height, true);

    ERHE_VERIFY(m_render_pass);

    erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(*m_render_pass.get());
    context.encoder = &encoder;

    // Starting render encoder clears render target texture(s)
    if (!do_render) {
        // ending render encoder applies multisample resolve, if applicabale
        //m_context.debug_renderer->release();
        return;
    }

    // TODO This would be? needed for basic (non-ImGui) viewports?
    // encoder.set_scissor_rect(context.viewport.x, context.viewport.y, context.viewport.width, context.viewport.height);

    m_context.app_message_bus->send_message(
        App_message{
            .update_flags = Message_flag_bit::c_flag_bit_render_scene_view,
            .scene_view   = this
        }
    );

    m_context.app_rendering ->render_viewport_main(context);
    m_context.app_rendering ->render_viewport_renderables(context); // This time with render encoder set
    m_context.debug_renderer->render(encoder, context.viewport);
    m_context.debug_renderer->end_frame();
    m_context.text_renderer ->render(encoder, context.viewport);
}

void Viewport_scene_view::set_window_viewport(erhe::math::Viewport viewport)
{
    // Needed for get_viewport_from_window()
    m_window_viewport = viewport;
    m_projection_viewport.x      = 0;
    m_projection_viewport.y      = 0;
    m_projection_viewport.width  = viewport.width;
    m_projection_viewport.height = viewport.height;
}

void Viewport_scene_view::set_is_scene_view_hovered(const bool is_hovered)
{
    SPDLOG_LOGGER_TRACE(log_scene_view, "{}->set_is_scene_view_hovered({})", get_name(), m_is_scene_view_hovered);
    m_is_scene_view_hovered = is_hovered;
}

void Viewport_scene_view::set_camera(const std::shared_ptr<erhe::scene::Camera>& camera)
{
    // TODO Add validation
    m_camera = camera;
}

auto Viewport_scene_view::is_scene_view_hovered() const -> bool
{
    SPDLOG_LOGGER_TRACE(log_scene_view, "{}->is_scene_view_hovered() = {}", get_name(), m_is_scene_view_hovered);
    return m_is_scene_view_hovered;
}

auto Viewport_scene_view::get_window_viewport() const -> const erhe::math::Viewport&
{
    return m_window_viewport;
}

auto Viewport_scene_view::get_projection_viewport() const -> const erhe::math::Viewport&
{
    return m_projection_viewport;
}

auto Viewport_scene_view::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_camera.lock();
}

auto Viewport_scene_view::get_perspective_scale() const -> float
{
    const auto camera = m_camera.lock();
    if (!camera) {
        return 1.0f;
    }
    const erhe::scene::Camera_projection_transforms camera_projection_transforms_ = camera->projection_transforms(m_projection_viewport);
    const glm::mat4 clip_from_view = camera_projection_transforms_.clip_from_camera.get_matrix();
    const glm::mat2 projection_top_left_2x2{clip_from_view};
    const glm::mat2 inverted_top_left_2x2 = glm::inverse(projection_top_left_2x2);
    const float x = inverted_top_left_2x2[0][0];
    const float y = inverted_top_left_2x2[1][1];
    const float w = static_cast<float>(m_projection_viewport.width);
    const float h = static_cast<float>(m_projection_viewport.height);
    const float vp_scale = 1000.0f / std::min(w, h);
    return std::min(x, y) * vp_scale;
}

auto Viewport_scene_view::get_rendergraph_node() -> erhe::rendergraph::Rendergraph_node*
{
    return this;
}

auto Viewport_scene_view::as_viewport_scene_view() -> Viewport_scene_view*
{
    return this;
}

auto Viewport_scene_view::as_viewport_scene_view() const -> const Viewport_scene_view*
{
    return this;
}

auto Viewport_scene_view::get_viewport_from_window(const glm::vec2 window_position) const -> glm::vec2
{
    const float content_x      = static_cast<float>(window_position.x) - m_window_viewport.x;
    const float content_y      = static_cast<float>(window_position.y) - m_window_viewport.y;
    const float content_flip_y = m_window_viewport.height - content_y;
    return glm::vec2{content_x, content_flip_y};
}

auto Viewport_scene_view::project_to_viewport(const glm::vec3 position_in_world) const -> std::optional<glm::vec3>
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

auto Viewport_scene_view::unproject_to_world(const glm::vec3 position_in_window) const -> std::optional<glm::vec3>
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

void Viewport_scene_view::update_pointer_2d_position(const glm::vec2 position_in_viewport)
{
    ERHE_PROFILE_FUNCTION();

    m_position_in_viewport = position_in_viewport;
}

void Viewport_scene_view::request_cursor_relative_hold(bool relative_hold_enable)
{
    m_relative_hold_enable = relative_hold_enable;
}

auto Viewport_scene_view::get_cursor_relative_hold() const -> bool
{
    return m_relative_hold_enable;
}

void Viewport_scene_view::update_hover(bool ray_only)
{
    // Note: Using reverse Z
    const auto near_position_in_world = get_position_in_world_viewport_depth(1.0f);
    const auto far_position_in_world  = get_position_in_world_viewport_depth(0.0f);

    // log_input_frame->info("Viewport_scene_view::update_hover");

    const auto camera = m_camera.lock();
    if (!near_position_in_world.has_value() || !far_position_in_world.has_value() || !camera || !m_is_scene_view_hovered) {
        reset_control_transform();
        reset_hover_slots();
        return;
    }

    set_world_from_control(near_position_in_world.value(), far_position_in_world.value());

    if (ray_only) {
        return;
    }

    const auto scene_root = m_scene_root.lock();
    if (scene_root) {
        scene_root->update_pointer_for_rendertarget_meshes(this);
    }

    if (m_context.id_renderer && m_context.id_renderer->enabled) {
        update_hover_with_id_render();
    } else {
        update_hover_with_raytrace();
    }

    update_grid_hover();

    if (m_hover_update_pending) {
        m_context.app_message_bus->send_message(
            App_message{
                .update_flags = Message_flag_bit::c_flag_bit_hover_mesh,
            }
        );
        m_hover_update_pending = false;
    }
}

void Viewport_scene_view::update_hover_with_id_render()
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
        SPDLOG_LOGGER_TRACE(log_scene_view, "pointer context hover not valid");
        return;
    }

    Hover_entry entry{
        .valid                      = id_query.valid,
        .scene_mesh_weak            = id_query.mesh,
        .scene_mesh_primitive_index = id_query.index_of_gltf_primitive_in_mesh,
        .position                   = get_position_in_world_viewport_depth(id_query.depth),
        .triangle                   = static_cast<uint32_t>(id_query.triangle_id) // TODO Consider these types
    };
    log_controller_ray->trace("id_query.triangle_id = {}", id_query.triangle_id);

    log_controller_ray->trace("position in world = {}", entry.position.value());

    std::shared_ptr<erhe::scene::Mesh> scene_mesh = entry.scene_mesh_weak.lock();
    if (scene_mesh) {
        const erhe::scene::Node* node = scene_mesh->get_node();
        ERHE_VERIFY(node != nullptr);
        const erhe::scene::Mesh_primitive& mesh_primitive = scene_mesh->get_primitives()[entry.scene_mesh_primitive_index];
        const erhe::primitive::Primitive&  primitive      = *mesh_primitive.primitive.get();
        const std::shared_ptr<erhe::primitive::Primitive_shape> shape = primitive.get_shape_for_raytrace();
        if (shape) {
            entry.geometry = shape->get_geometry();
            if (entry.geometry) {
                const GEO::Mesh& geo_mesh = entry.geometry->get_mesh();
                entry.facet = shape->get_mesh_facet_from_triangle(entry.triangle);
                if (entry.facet != GEO::NO_INDEX) {
                    log_controller_ray->trace("hover facet = {}", entry.facet);

                    const bool       negative_determinant   = (node->get_flag_bits() & erhe::Item_flags::negative_determinant) == erhe::Item_flags::negative_determinant;
                    const GEO::vec3f facet_normal           = mesh_facet_normalf(geo_mesh, entry.facet);
                    const glm::vec3  local_normal           = to_glm_vec3(facet_normal);
                    const glm::mat4  world_from_node        = node->world_from_node();
                    const glm::mat4  normal_world_from_node = glm::transpose(glm::adjugate(world_from_node));
                    const glm::vec3  normal_in_world        = glm::vec3{normal_world_from_node * glm::vec4{local_normal, 0.0f}};
                    const glm::vec3  unit_normal            = glm::normalize(normal_in_world);
                    entry.normal = negative_determinant ? -unit_normal : unit_normal;
                    log_controller_ray->trace("hover normal = {}", entry.normal.value());
                } else {
                    log_controller_ray->trace("hover facet = missing triangle to facet mapping");
                }
            }
        }
    }

    using namespace erhe::utility;

    const uint64_t flags = (id_query.mesh != nullptr) && scene_mesh ? scene_mesh->get_flag_bits() : 0;

    const bool hover_content      = id_query.mesh && test_bit_set(flags, erhe::Item_flags::content     );
    const bool hover_tool         = id_query.mesh && test_bit_set(flags, erhe::Item_flags::tool        );
    const bool hover_brush        = id_query.mesh && test_bit_set(flags, erhe::Item_flags::brush       );
    const bool hover_rendertarget = id_query.mesh && test_bit_set(flags, erhe::Item_flags::rendertarget);
    SPDLOG_LOGGER_TRACE(
        log_controller_ray,
        "hover mesh = {} primitive index = {} facet {} {}{}{}{}",
        entry.scene_mesh ? entry.scene_mesh->get_name() : "()",
        entry.scene_mesh_primitive_index,
        entry.facet,
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

auto Viewport_scene_view::get_position_in_viewport() const -> std::optional<glm::vec2>
{
    return m_position_in_viewport;
}

auto Viewport_scene_view::get_position_in_world_viewport_depth(const float viewport_depth) const -> std::optional<glm::vec3>
{
    const auto camera = m_camera.lock();
    if (!m_position_in_viewport.has_value() || !camera) {
        return {};
    }

    const float depth_range_near     = 0.0f;
    const float depth_range_far      = 1.0f;
    const auto  position_in_viewport = glm::vec3{
        m_position_in_viewport.value().x,
        m_position_in_viewport.value().y,
        viewport_depth
    };
    const erhe::math::Viewport&                     vp                    = get_projection_viewport();
    const erhe::scene::Camera_projection_transforms projection_transforms = camera->projection_transforms(vp);
    const glm::mat4                                 world_from_clip       = projection_transforms.clip_from_world.get_inverse_matrix();

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

auto Viewport_scene_view::get_shadow_render_node() const -> Shadow_render_node*
{
    Rendergraph_node*   input_node         = get_consumer_input_node(erhe::rendergraph::Rendergraph_node_key::shadow_maps);
    Shadow_render_node* shadow_render_node = static_cast<Shadow_render_node*>(input_node);
    return shadow_render_node;
}

auto Viewport_scene_view::get_show_navigation_gizmo() const -> bool
{
    return m_show_navigation_gizmo;
}

void Viewport_scene_view::register_toolbar_callback(std::function<void()> callback)
{
    m_toolbar_callbacks.push_back(std::move(callback));
}

auto Viewport_scene_view::viewport_toolbar() -> bool
{
    bool hovered = false;

    {
        ImGui::PushID("viewport toolbar debug");

        const auto navigation_gizmo_pressed = erhe::imgui::make_button("N", m_show_navigation_gizmo ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal);
        ImGui::SameLine();
        if (ImGui::IsItemHovered()) {
            hovered = true;
            ImGui::SetTooltip("Show/Hide Navigation Gizmo");
        }
        if (navigation_gizmo_pressed) {
            m_show_navigation_gizmo = !m_show_navigation_gizmo;
        }
        ImGui::PopID();
    }

    m_context.scene_views->viewport_toolbar(*this, hovered);

    //// TODO Tool_flags::viewport_toolbar
    m_context.selection_tool->viewport_toolbar(hovered);
    m_context.transform_tool->viewport_toolbar(hovered);
    //// m_context.grid_tool->viewport_toolbar(hovered);
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
    const bool combo_used     = m_context.app_scenes->scene_combo("##Scene", scene_root_raw, false);
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

    for (const auto& callback : m_toolbar_callbacks) {
        callback();
    }

    ImGui::SetNextItemWidth(80.0f);
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

void Viewport_scene_view::set_shader_stages_variant(Shader_stages_variant variant)
{
    m_shader_stages_variant = variant;
}

auto Viewport_scene_view::get_shader_stages_variant() const -> Shader_stages_variant
{
    return m_shader_stages_variant;
}

auto Viewport_scene_view::get_override_shader_stages() const -> const erhe::graphics::Shader_stages*
{
    return m_context.programs->get_variant_shader_stages(m_shader_stages_variant);
}

auto Viewport_scene_view::get_closest_point_on_line(const glm::vec3 P0, const glm::vec3 P1) -> std::optional<glm::vec3>
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
        const auto Q0_opt = get_position_in_world_viewport_depth(1.0);
        const auto Q1_opt = get_position_in_world_viewport_depth(0.0);
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

auto Viewport_scene_view::get_closest_point_on_plane(const glm::vec3 N, const glm::vec3 P) -> std::optional<glm::vec3>
{
    ERHE_PROFILE_FUNCTION();

    using vec3 = glm::vec3;

    const auto Q0_opt = get_position_in_world_viewport_depth(1.0);
    const auto Q1_opt = get_position_in_world_viewport_depth(0.0);
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

}
