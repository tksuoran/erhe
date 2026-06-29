#include "tools/hud.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_windows.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "tools/tools.hpp"
#include "quad_view.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_host.hpp"

#include "erhe_commands/commands.hpp"
#include "config/generated/hud_config.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#include <imgui/imgui.h>

namespace editor {

using glm::vec3;

#pragma region Commands
Hud_drag_command::Hud_drag_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Hud.drag"}
    , m_context{context}
{
}

void Hud_drag_command::try_ready()
{
    if (get_command_state() != erhe::commands::State::Inactive) {
        return;
    }

    if (m_context.hud->try_begin_drag()) {
        set_active();
    }
}

auto Hud_drag_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }

    m_context.hud->on_drag();

    return true;
}

void Hud_drag_command::on_inactive()
{
    if (
        (get_command_state() == erhe::commands::State::Ready ) ||
        (get_command_state() == erhe::commands::State::Active)
    ) {
        m_context.hud->end_drag();
    }
}

Toggle_hud_visibility_command::Toggle_hud_visibility_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Hud.toggle_visibility"}
    , m_context{context}
{
}

auto Toggle_hud_visibility_command::try_call() -> bool
{
    m_context.hud->toggle_mesh_visibility();
    return true;
}
#pragma endregion Commands

Hud::Hud(
    const Hud_config&                  hud_config,
    erhe::commands::Commands&          commands,
    erhe::graphics::Device&            graphics_device,
    erhe::imgui::Imgui_renderer&       imgui_renderer,
    erhe::rendergraph::Rendergraph&    rendergraph,
    App_context&                       app_context,
    App_message_bus&                   app_message_bus,
    App_windows&                       app_windows,
    Headset_view&                      headset_view,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    Tools&                             tools
)
    : Tool                                {app_context}
    , m_toggle_visibility_command         {commands, app_context}
#if defined(ERHE_XR_LIBRARY_OPENXR)
    , m_drag_command                      {commands, app_context}
    , m_drag_float_redirect_update_command{commands, m_drag_command}
    , m_drag_float_enable_command         {commands, m_drag_float_redirect_update_command, 0.3f, 0.1f}
    , m_drag_bool_redirect_update_command {commands, m_drag_command}
    , m_drag_bool_enable_command          {commands, m_drag_bool_redirect_update_command}
#endif
{
    ERHE_PROFILE_FUNCTION();

    // Stored for the deferred attach_to_scene(): the scene-dependent Quad_view is
    // built there once a scene exists (the scene.create startup command creates
    // the scene after all parts are constructed), not at construction time.
    m_graphics_device = &graphics_device;
    m_imgui_renderer  = &imgui_renderer;
    m_rendergraph     = &rendergraph;
    m_mesh_memory     = &mesh_memory;
    m_headset_view    = &headset_view;
    m_app_windows     = &app_windows;
    m_width           = hud_config.width;
    m_height          = hud_config.height;
    m_ppm             = hud_config.ppm;

    m_enabled = hud_config.enabled;
    m_x       = hud_config.x;
    m_y       = hud_config.y;
    m_z       = hud_config.z;

    if (!m_enabled) {
        return;
    }

    set_description  ("Hud");
    set_flags        (Tool_flags::background);
    register_tool    (tools);

    commands.register_command   (&m_toggle_visibility_command);
    commands.bind_command_to_key(&m_toggle_visibility_command, erhe::window::Key_e, true);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::Headset*    headset  = headset_view.get_headset();
    erhe::xr::Xr_actions* xr_right = (headset != nullptr) ? headset->get_actions_right() : nullptr;
    if (xr_right != nullptr) {
        commands.bind_command_to_xr_boolean_action(&m_toggle_visibility_command, xr_right->menu_click, erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_toggle_visibility_command, xr_right->b_click,    erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_float_action  (&m_drag_float_enable_command, xr_right->squeeze_value);
        commands.bind_command_to_update           (&m_drag_float_redirect_update_command);
        commands.bind_command_to_xr_boolean_action(&m_drag_bool_enable_command, xr_right->squeeze_click, erhe::commands::Button_trigger::Any);
        commands.bind_command_to_update           (&m_drag_bool_redirect_update_command);
    }
    m_drag_command.set_host(this);
#else
    static_cast<void>(headset_view);
#endif

    // The scene-dependent Quad_view is built later in attach_to_scene(), once the
    // scene.create startup command has created a scene (see Phase-3 wiring).

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [&](Hover_scene_view_message& message) {
            Tool::on_message(message);
        }
    );
}

void Hud::attach_to_scene(const std::shared_ptr<Scene_root>& scene_root)
{
    if (!m_enabled) {
        return;
    }

    // current_command_buffer is the init command buffer during the startup script,
    // or the per-frame buffer when a scene is loaded later; either is valid here.
    ERHE_VERIFY(m_context.current_command_buffer != nullptr);
    ERHE_VERIFY(scene_root);
    m_quad_view = std::make_unique<Quad_view>(
        m_context,
        *m_graphics_device,
        *m_context.current_command_buffer,
        *m_mesh_memory,
        *m_imgui_renderer,
        *m_rendergraph,
        *scene_root,
        m_headset_view,
        m_width,
        m_height,
        m_ppm,
        "Hud Viewport",
        true
    );

    // The Hud is placed at a fixed world pose (not billboarded toward the user
    // like the Hotbar), so make it double-sided to stay visible when viewed
    // from behind. (Interim: the back face is readable but horizontally flipped
    // in world space; a true two-sided mirror would need a flipped image copy.)
    m_quad_view->set_double_sided(true);

    // Depth-test the Hud against the scene so geometry can occlude it, and keep
    // it composited in front of the Hotbar (higher composition order = on top;
    // the Hotbar uses the default order 0).
    m_quad_view->set_depth_tested(true);
    m_quad_view->set_composition_order(1);

    // The Hud parents its node once to the scene root and only updates the
    // world transform thereafter (see update_node_transform).
    if (erhe::scene::Node* node = m_quad_view->get_node()) {
        node->set_parent(scene_root->get_scene().get_root_node());
    }

    Rendertarget_imgui_host* host = m_quad_view->get_imgui_host();

#if defined(ERHE_XR_LIBRARY_OPENXR)
    // In OpenXR mode the Headset_view_node must be the rendergraph sink
    // (everything before it, nothing after). Otherwise this orphan
    // Rendertarget_imgui_host would be sorted after the headset node and
    // try to record into the device cb after Xr_session::render_frame
    // already submitted the frame, asserting in
    // Device_impl::acquire_shared_command_buffer.
    if (m_context.OpenXR) {
        erhe::rendergraph::Rendergraph_node* headset_node = m_headset_view->get_rendergraph_node();
        if (headset_node != nullptr) {
            m_rendergraph->connect(
                erhe::rendergraph::Rendergraph_node_key::rendertarget_texture,
                host,
                headset_node
            );
        }
    }
#endif

    host->set_begin_callback(
        [this](erhe::imgui::Imgui_host& imgui_host) {
            m_app_windows->viewport_menu(imgui_host);
        }
    );

    set_mesh_visibility(true);
}

Hud::~Hud() noexcept = default;

auto Hud::get_rendertarget_imgui_viewport() const -> std::shared_ptr<Rendertarget_imgui_host>
{
    return (m_quad_view != nullptr) ? m_quad_view->get_imgui_host_shared() : nullptr;
}

auto Hud::world_from_node() const -> glm::mat4
{
    ERHE_VERIFY(m_quad_view != nullptr);
    erhe::scene::Node* node = m_quad_view->get_node();
    ERHE_VERIFY(node != nullptr);
    return node->world_from_node();
}

auto Hud::node_from_world() const -> glm::mat4
{
    ERHE_VERIFY(m_quad_view != nullptr);
    erhe::scene::Node* node = m_quad_view->get_node();
    ERHE_VERIFY(node != nullptr);
    return node->node_from_world();
}

auto Hud::intersect_ray(const glm::vec3& ray_origin_in_world, const glm::vec3& ray_direction_in_world) -> std::optional<glm::vec3>
{
    const glm::vec3 ray_origin_in_hud    = glm::vec3{node_from_world() * glm::vec4{ray_origin_in_world,    1.0f}};
    const glm::vec3 ray_direction_in_hud = glm::vec3{node_from_world() * glm::vec4{ray_direction_in_world, 0.0f}};
    const auto intersection = erhe::math::intersect_plane<float>(
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 0.0f},
        ray_origin_in_hud,
        ray_direction_in_hud
    );
    if (!intersection.has_value()) {
        return {};
    }
    const glm::vec3 position_in_node = ray_origin_in_hud + intersection.value() * ray_direction_in_hud;

    Rendertarget_mesh* rendertarget_mesh = (m_quad_view != nullptr) ? m_quad_view->get_rendertarget_mesh() : nullptr;
    if (rendertarget_mesh == nullptr) {
        return {};
    }
    const auto half_width  = 0.5f * rendertarget_mesh->get_width();
    const auto half_height = 0.5f * rendertarget_mesh->get_height();

    if (
        (position_in_node.x < -half_width ) ||
        (position_in_node.x >  half_width ) ||
        (position_in_node.z < -half_height) ||
        (position_in_node.z >  half_height)
    ) {
        return {};
    }

    return glm::vec3{
        world_from_node() * glm::vec4{position_in_node, 1.0f}
    };
}

auto Hud::try_begin_drag() -> bool
{
    m_node_from_control.reset();
    m_path_b_dragging = false;

    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }

    const auto world_from_control_opt = scene_view->get_world_from_control();
    if (!world_from_control_opt.has_value()) {
        return false;
    }

    if ((m_quad_view != nullptr) && m_quad_view->uses_composition_layer()) {
        // Path B: there is no scene mesh to hover. Grab the quad when the
        // controller ray points at it, then attach it rigidly to the controller
        // for the duration of the drag.
        const glm::mat4 world_from_control = world_from_control_opt.value();
        const glm::vec3 ray_origin    = glm::vec3{world_from_control[3]};
        const glm::vec3 ray_direction = glm::vec3{world_from_control * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}};
        if (!m_quad_view->intersect_ray(ray_origin, ray_direction).has_value()) {
            return false;
        }
        m_drag_control_from_quad = glm::inverse(world_from_control) * m_quad_view->get_world_from_quad();
        m_path_b_dragging = true;
        return true;
    }

    const auto* drag_entry = scene_view->get_nearest_hover(Hover_entry::rendertarget_bit);
    if ((drag_entry == nullptr) || !drag_entry->valid) {
        return false;
    }

    std::shared_ptr<erhe::scene::Mesh> drag_scene_mesh = drag_entry->scene_mesh_weak.lock();
    if (!drag_scene_mesh) {
        return false;
    }
    erhe::scene::Node* node = drag_scene_mesh->get_node();
    if (node == nullptr) {
        return false;
    }

    std::shared_ptr<erhe::scene::Node> drag_node = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());
    m_drag_node = drag_node;
    if (!drag_node) {
        return false;
    }

    const glm::mat4 world_from_control = world_from_control_opt.value();
    const glm::mat4 node_from_world    = drag_node->node_from_world();
    const glm::mat4 world_from_node    = drag_node->world_from_node();
    m_node_from_control = node_from_world * world_from_control;
    return true;
}

void Hud::on_drag()
{
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    if (m_path_b_dragging) {
        const auto world_from_control_opt = scene_view->get_world_from_control();
        if (!world_from_control_opt.has_value() || (m_quad_view == nullptr)) {
            return;
        }
        const glm::mat4 world_from_quad = world_from_control_opt.value() * m_drag_control_from_quad;
        m_quad_view->set_world_from_node(world_from_quad);
        return;
    }

    const auto control_from_world_opt = scene_view->get_control_from_world();
    if (!control_from_world_opt.has_value() || !m_node_from_control.has_value()) {
        return;
    }

    auto drag_node = m_drag_node.lock();
    if (!drag_node) {
        return;
    }

    const glm::mat4 control_from_world = control_from_world_opt.value();
    const glm::mat4 node_from_world    = m_node_from_control.value() * control_from_world;
    drag_node->set_node_from_world(node_from_world);
}

void Hud::end_drag()
{
    m_node_from_control.reset();
    m_drag_node.reset();
    m_path_b_dragging = false;
}

void Hud::update_node_transform(const glm::mat4& world_from_hud)
{
    if (!m_quad_view) {
        return;
    }

    m_world_from_hud = world_from_hud;
    const glm::mat4 offset = erhe::math::create_translation(m_x, m_y, m_z);
    const glm::mat4 final_transform = m_world_from_hud * offset;
    m_quad_view->set_world_from_node(final_transform);
}

void Hud::tool_render(const Render_context&)
{
}

void Hud::imgui()
{
    ImGui::Checkbox("Mesh visible", &m_mesh_visible);

    float rendertarget_mesh_lod_bias = Rendertarget_mesh::get_mesh_lod_bias();
    ImGui::SliderFloat("Hud LoD Bias", &rendertarget_mesh_lod_bias, -8.0f, 8.0f);
    if (ImGui::IsItemEdited()) {
        Rendertarget_mesh::set_mesh_lod_bias(rendertarget_mesh_lod_bias);
    }

    const bool x_changed = ImGui::DragFloat("X", &m_x, 0.0001f);
    const bool y_changed = ImGui::DragFloat("Y", &m_y, 0.0001f);
    const bool z_changed = ImGui::DragFloat("Z", &m_z, 0.0001f);
    if (x_changed || y_changed || z_changed) {
        update_node_transform(m_world_from_hud);
    }
}

auto Hud::toggle_mesh_visibility() -> bool
{
    if (!m_enabled) {
        return false;
    }

    set_mesh_visibility(!m_mesh_visible);
    return m_mesh_visible;
}

void Hud::set_mesh_visibility(const bool value)
{
    if (!m_enabled || !m_quad_view) {
        return;
    }

    m_mesh_visible = value;

    Scene_view* hover_scene_view = get_hover_scene_view();
    if (hover_scene_view != nullptr) {
        const auto& world_from_control_opt = hover_scene_view->get_world_from_control();
        if (world_from_control_opt.has_value()) {
            update_node_transform(world_from_control_opt.value());
        }
    }

    m_quad_view->set_visible(m_mesh_visible);
}

}
