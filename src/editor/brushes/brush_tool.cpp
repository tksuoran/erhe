#include "brushes/brush_tool.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "graphics/icon_set.hpp"
#include "grid/grid.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"
#include "windows/item_tree_window.hpp"
#include "time.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_utility/bit_helpers.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#include <imgui/imgui.h>

#include <geogram/basic/matrix.h>

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace editor {

#pragma region Commands
Brush_preview_command::Brush_preview_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Brush_tool.motion_preview"}
    , m_context{context}
{
}

auto Brush_preview_command::try_call() -> bool
{
    if ((get_command_state() != erhe::commands::State::Active) || !m_context.brush_tool->is_enabled()) {
        return false;
    }
    m_context.brush_tool->on_motion();
    return true;
}

Brush_insert_command::Brush_insert_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Brush_tool.insert"}
    , m_context{context}
{
}

void Brush_insert_command::try_ready()
{
    if (m_context.brush_tool->try_insert_ready()) {
        set_ready();
    }
}

auto Brush_insert_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        return false;
    }
    const bool consumed = m_context.brush_tool->try_insert();
    set_inactive();
    return consumed;
}

Brush_pick_command::Brush_pick_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Brush_tool.pick"}
    , m_context{context}
{
}

auto Brush_pick_command::try_call() -> bool
{
    return m_context.brush_tool->try_pick();
}

Brush_rotate_command::Brush_rotate_command(erhe::commands::Commands& commands, App_context& context, int direction)
    : Command    {commands, "Brush_tool.rotate"}
    , m_direction{direction}
    , m_context  {context}
{
}

auto Brush_rotate_command::try_call() -> bool
{
    const bool consumed = m_context.brush_tool->try_rotate(m_direction);
    set_inactive();
    return consumed;
}

#pragma endregion Commands

Brush_tool::Brush_tool(
    erhe::commands::Commands& commands,
    App_context&              context,
    App_message_bus&          app_message_bus,
    Headset_view&             headset_view,
    Icon_set&                 icon_set,
    Tools&                    tools
)
    : Tool                            {context}
    , m_preview_command               {commands, context}
    , m_insert_command                {commands, context}
    , m_pick_command                  {commands, context}
    , m_rotate_cw_command             {commands, context, 1}
    , m_rotate_ccw_command            {commands, context, -1}
    , m_pick_using_float_input_command{commands, m_pick_command, 0.6f, 0.4f}
    , m_toggle_brush_preview_command  {
        commands,
        "Brush.toggle_preview",
        [this]() -> bool {
            m_show_preview = !m_show_preview;
            update_preview_mesh();
            return true;
        }
    }
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority(Brush_tool::c_priority);
    set_description  ("Brush Tool");
    set_flags        (Tool_flags::toolbox);
    set_icon         (icon_set.custom_icons, icon_set.icons.brush_big);

    commands.register_command(&m_preview_command);
    commands.register_command(&m_insert_command);
    commands.register_command(&m_pick_command);
    commands.register_command(&m_pick_using_float_input_command);
    commands.register_command(&m_rotate_cw_command);
    commands.register_command(&m_rotate_ccw_command);
    commands.register_command(&m_toggle_brush_preview_command);
    commands.bind_command_to_update      (&m_preview_command);
    commands.bind_command_to_mouse_button(&m_insert_command,     erhe::window::Mouse_button_right, true);
    //commands.bind_command_to_mouse_button(&m_rotate_cw_command,  erhe::window::Mouse_button_x1,    true);
    //commands.bind_command_to_mouse_button(&m_rotate_ccw_command, erhe::window::Mouse_button_x2,    true);
    commands.bind_command_to_key(&m_rotate_cw_command, erhe::window::Key_z);
    commands.bind_command_to_key(&m_rotate_ccw_command, erhe::window::Key_x);
    commands.bind_command_to_key(&m_toggle_brush_preview_command, erhe::window::Key_c);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::Headset*    headset  = headset_view.get_headset();
    erhe::xr::Xr_actions* xr_right = (headset != nullptr) ? headset->get_actions_right() : nullptr;
    if (xr_right != nullptr) {
        commands.bind_command_to_xr_boolean_action(&m_insert_command, xr_right->trigger_click, erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_insert_command, xr_right->a_click,       erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_float_action  (&m_pick_using_float_input_command, xr_right->squeeze_value);
        //commands.bind_command_to_xr_boolean_action(&m_pick_command,   xr_right->b_click,       erhe::commands::Button_trigger::Button_pressed);
    }
#else
    static_cast<void>(headset_view);
#endif

    tools.register_tool(this);

    app_message_bus.add_receiver(
        [&](App_message& message) {
            on_message(message);
        }
    );

    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "scene");
    ini.get("object_scale", m_grid_scale);

    m_preview_command               .set_host(this);
    m_insert_command                .set_host(this);
    m_pick_command                  .set_host(this);
    m_rotate_cw_command             .set_host(this);
    m_rotate_ccw_command            .set_host(this);
    m_pick_using_float_input_command.set_host(this);
    m_toggle_brush_preview_command  .set_host(this);
}

void Brush_tool::on_message(App_message& message)
{
    Scene_view* const old_scene_view = get_hover_scene_view();

    Tool::on_message(message);
    using namespace erhe::utility;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_mesh)) {
        on_motion();
    }
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
        if (message.scene_view != old_scene_view) {
            bool visible = message.scene_view && (get_hover_scene_view() == message.scene_view);
            if (!visible) {
                remove_preview_mesh();
            }
        }
    }
}

void Brush_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (new_priority < old_priority) {
        disable_command_host();
        m_preview_command.set_inactive();
        m_hover = Hover_entry{};
        remove_preview_mesh();
    }
    if (new_priority > old_priority) {
        enable_command_host();
        m_preview_command.set_active();
        on_motion();
    }
}

void Brush_tool::remove_preview_mesh()
{
    // Remove mesh attachment *before* removing node
    if (m_preview_mesh) {
        m_preview_mesh.reset();
    }
    if (m_preview_node) {
        // Do not remove this. It is not possible to rely on reset calling destructor,
        // because parent will have shared_ptr to the child.
        m_preview_node->set_parent({});
        m_preview_node.reset();
    }

    m_hover_frame.reset();
}

auto Brush_tool::try_rotate(int direction) -> bool
{
    //remove_preview_mesh();
    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    if (!hover_scene_mesh) {
        return false;
    }

    erhe::scene::Node* node = hover_scene_mesh->get_node();
    if (node == nullptr) {
        return false;
    }
    std::shared_ptr<erhe::Item_base>   node_item_base = node->shared_from_this();
    std::shared_ptr<erhe::scene::Node> node_shared    = std::dynamic_pointer_cast<erhe::scene::Node>(node_item_base);

    std::shared_ptr<Brush_placement> brush_placement = get_brush_placement(node);
    if (!brush_placement) {
        return false;
    }

    std::shared_ptr<Brush> brush = brush_placement->get_brush();
    if (!brush) {
        return false;
    }

    if (!m_hover.geometry) {
        return false;
    }

    m_context.time->finish_all_transform_animations(*m_context.app_message_bus);

    erhe::geometry::Geometry& geometry = *m_hover.geometry.get();
    const GEO::Mesh& geo_mesh           = geometry.get_mesh();
    GEO::index_t     facet              = brush_placement->get_facet();
    ERHE_VERIFY(facet < geo_mesh.facets.nb());
    GEO::index_t     facet_corner_count = geo_mesh.facets.nb_corners(facet);
    GEO::index_t     old_corner         = brush_placement->get_corner();
    GEO::index_t     new_corner         = (old_corner + facet_corner_count + direction) % facet_corner_count;

    Reference_frame initial_frame{geo_mesh, facet, 0, old_corner, Frame_orientation::in};
    Reference_frame updated_frame{geo_mesh, facet, 0, new_corner, Frame_orientation::in};

    GEO::mat4f initial_brush_transform_ = initial_frame.transform(0.0f);
    GEO::mat4f updated_brush_transform_ = updated_frame.transform(0.0f);
    glm::mat4  initial_brush_transform  = to_glm_mat4(initial_brush_transform_);
    glm::mat4  updated_brush_transform  = to_glm_mat4(updated_brush_transform_);
    glm::mat4  brush_update_transform   = updated_brush_transform * glm::inverse(initial_brush_transform);
    glm::mat4  initial_node_transform   = node->parent_from_node();
    glm::mat4  updated_node_transform   = initial_node_transform * brush_update_transform;

    auto node_operation = std::make_shared<Node_transform_operation>(
        Node_transform_operation::Parameters{
            .node                    = node_shared,
            .parent_from_node_before = node_shared->parent_from_node_transform(),
            .parent_from_node_after  = erhe::scene::Transform{updated_node_transform},
            .time_duration           = 0.25f
        }
    );
    m_context.operation_stack->queue(node_operation);

    brush_placement->set_corner(new_corner);
    return true;
}

auto Brush_tool::try_insert_ready() -> bool
{
    return is_enabled() &&
        (
            !m_hover.scene_mesh_weak.expired() ||
            !m_hover.grid_weak.expired()
        );
}

auto Brush_tool::get_hover_brush() const -> std::shared_ptr<Brush>
{
    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    if (!hover_scene_mesh) {
        return {};
    }

    erhe::scene::Node* node = hover_scene_mesh->get_node();
    if (node == nullptr) {
        return {};
    }

    std::shared_ptr<Brush_placement> brush_placement = get_brush_placement(node);
    if (!brush_placement) {
        return {};
    }

    std::shared_ptr<Brush> brush = brush_placement->get_brush();
    return brush;
}

auto Brush_tool::try_pick() -> bool
{
    std::shared_ptr<Brush> brush = get_hover_brush();
    if (!brush) {
        return false;
    }
    m_context.selection->set_selection({brush});
    return true;
}

auto Brush_tool::try_insert(Brush* brush) -> bool
{
    if (brush == nullptr) {
        brush = m_context.selection->get_last_selected<Brush>().get();
        if (brush == nullptr) {
            return false;
        }
    }

    //update_preview_mesh_node_transform();

    if (!m_hover.position.has_value() || !brush) {
        return false;
    }

#if !defined(NDEBUG)
        m_context.app_scenes->sanity_check();
#endif

    do_insert_operation(*brush);

#if !defined(NDEBUG)
        m_context.app_scenes->sanity_check();
#endif

    remove_preview_mesh();

#if !defined(NDEBUG)
        m_context.app_scenes->sanity_check();
#endif

    return true;
}

void Brush_tool::on_motion()
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    const Hover_entry* nearest_hover = scene_view->get_nearest_hover(
        Hover_entry::content_bit |
        Hover_entry::grid_bit    |
        Hover_entry::rendertarget_bit
    );
    if ((nearest_hover == nullptr) || (nearest_hover->slot == Hover_entry::rendertarget_slot)) {
        return;
    }
    m_hover = *nearest_hover;

    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    if (hover_scene_mesh) {
        erhe::scene::Node* hover_node = hover_scene_mesh->get_node();
        if (hover_node != nullptr) {
            if (m_hover.position.has_value()) {
                m_hover.position = hover_node->transform_direction_from_world_to_local(m_hover.position.value());
            }
        }
    }

    update_preview_mesh();
}

void Brush_tool::preview_drag_and_drop(std::shared_ptr<Brush> brush)
{
    if (brush) {
        m_show_preview = true;
    }
    m_drag_and_drop_brush = brush;
    on_motion();
}

auto Brush_tool::get_placement_facet() const -> GEO::index_t
{
    return m_brush_placement_frame.has_value() ? m_brush_placement_frame.value().m_facet : GEO::NO_INDEX;
}

auto Brush_tool::get_placement_corner0() const -> GEO::index_t
{
    return m_brush_placement_frame.has_value() ? m_brush_placement_frame.value().m_corner0 : GEO::NO_INDEX;
}

auto Brush_tool::update_hover_frame_from_mesh() -> bool
{
    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    if (
        !hover_scene_mesh ||
        (m_hover.scene_mesh_primitive_index == std::numeric_limits<std::size_t>::max()) ||
        (!m_hover.geometry)
    ) {
        return false;
    }

    const erhe::scene::Node* node = hover_scene_mesh->get_node();
    if (node == nullptr) {
        return false;
    }

    auto* geometry = m_hover.geometry.get();
    const GEO::Mesh& geo_mesh = geometry->get_mesh();
    ERHE_VERIFY(m_hover.facet < geo_mesh.facets.nb());

    GEO::index_t corner = 0;
    Reference_frame hover_frame{geo_mesh, m_hover.facet, 0, corner, Frame_orientation::out};

    if (!m_snap_to_hover_polygon && m_hover.position.has_value()) {
        hover_frame.m_centroid = to_geo_vec3f(m_hover.position.value());
    }

    m_world_from_hover         = node->world_from_node();
    m_hover_frame              = hover_frame;
    m_hover_facet_corner_count = geo_mesh.facets.nb_corners(m_hover.facet);
    return true;
}

auto Brush_tool::update_brush_frame(Brush& brush) -> bool
{
    if (!m_hover_frame.has_value()) {
        return false;
    }

    const Reference_frame& hover_frame = m_hover_frame.value();

    if (m_hover_facet_corner_count.has_value() && m_use_matching_face) {
        m_brush_placement_frame = brush.get_reference_frame(
            m_hover_facet_corner_count.value(),
            static_cast<uint32_t>(m_polygon_offset),
            static_cast<uint32_t>(m_corner_offset)
        );
    }

    if (!m_brush_placement_frame.has_value()) {
        const size_t selected_corner_count = m_selected_corner_count.has_value() ? m_selected_corner_count.value() : brush.get_max_corner_count();
        m_brush_placement_frame = brush.get_reference_frame(
            static_cast<uint32_t>(selected_corner_count),
            static_cast<uint32_t>(m_polygon_offset),
            static_cast<uint32_t>(m_corner_offset)
        );
    }

    Reference_frame& brush_frame = m_brush_placement_frame.value();

    ERHE_VERIFY(brush_frame.scale() != 0.0f);
    
    if (m_scale_to_match) {
        if (m_hover.slot == Hover_entry::grid_slot) {
            m_transform_scale = m_grid_scale;
        } else {
            const float hover_scale = hover_frame.scale();
            const float brush_scale = brush_frame.scale();
            const float scale       = (brush_scale != 0.0f) ? static_cast<float>(hover_scale / brush_scale) : 1.0f;
    
            m_transform_scale = m_scale * scale;
        }
        if (m_transform_scale != 1.0f) {
            const glm::mat4 scale_transform = erhe::math::create_scale(m_transform_scale);
            brush_frame.transform_by(to_geo_mat4f(scale_transform));
        }
    }

    const GEO::mat4f hover_transform = hover_frame.transform(0.0f);
    const GEO::mat4f brush_transform = brush_frame.transform(0.0f); // hover distance
    const GEO::mat4f inverse_brush   = brush_transform.inverse();
    const GEO::mat4f align           = hover_transform * inverse_brush;
    m_hover_transform = to_glm_mat4(hover_transform);
    m_align_transform = to_glm_mat4(align);
    return true;
}

auto Brush_tool::get_world_from_grid_hover_point() const -> glm::mat4
{
    std::shared_ptr<const Grid> hover_grid = m_hover.grid_weak.lock();
    if (!hover_grid) {
        return glm::mat4{1};
    }

    const glm::vec3 position_in_grid0 = hover_grid->grid_from_world() * glm::vec4{m_hover.position.value(), 1.0f};
    const glm::vec3 position_in_grid  = m_snap_to_grid ? hover_grid->snap_grid_position(position_in_grid0) : position_in_grid0;
    const glm::mat4 offset            = erhe::math::create_translation<float>(position_in_grid);
    const glm::mat4 world_from_grid   = hover_grid->world_from_grid() * offset;

    return world_from_grid;
}

auto Brush_tool::update_hover_frame_from_grid() -> bool
{
    std::shared_ptr<const Grid> hover_grid = m_hover.grid_weak.lock();
    if (!hover_grid || !m_hover.position.has_value()) {
        return false;
    }

    const Grid& grid = *hover_grid.get();
    Reference_frame hover_frame{grid, to_geo_vec3f(m_hover.position.value())};

    m_world_from_hover = hover_grid->world_from_grid();
    m_hover_frame      = hover_frame;
    return true;
}

void Brush_tool::update_preview_mesh_node_transform()
{
    m_hover_facet_corner_count.reset();
    m_brush_placement_frame.reset();
    m_world_from_hover.reset();
    m_hover_transform.reset();
    m_align_transform.reset();

    auto shared_brush = m_drag_and_drop_brush ? m_drag_and_drop_brush : m_context.selection->get_last_selected<Brush>();
    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        hover_grid       = m_hover.grid_weak.lock();
    if (
        !shared_brush ||
        !m_hover.position.has_value() ||
        !m_preview_mesh ||
        !m_preview_node ||
        (!hover_scene_mesh && !hover_grid)
    ) {
        return;
    }

    const bool got_hover_frame_from_mesh = update_hover_frame_from_mesh();
    const bool got_hover_frame_from_grid = !got_hover_frame_from_mesh && update_hover_frame_from_grid();
    if (!got_hover_frame_from_mesh && !got_hover_frame_from_grid) {
        return;
    }

    Brush&     brush          = *shared_brush.get();
    const bool brush_frame_ok = update_brush_frame(brush);
    if (!brush_frame_ok || !m_align_transform.has_value()) {
        return;
    }

    const Brush::Scaled& brush_scaled = brush.get_scaled(m_transform_scale);
    const glm::mat4&     transform    = m_align_transform.value();

    // TODO Unparent, to remove raytrace primitives to raytrace scene.
    //      This is a workaround for clear_primitives() issue below
    m_preview_node->set_parent({});

    const std::shared_ptr<erhe::primitive::Primitive_render_shape> shape = m_preview_mesh->get_primitives().front().render_shape;
    ERHE_VERIFY(shape);

    // TODO Brush mesh does not really need RT primitives at all

    const std::shared_ptr<erhe::primitive::Material>& material = m_preview_mesh->get_primitives().front().material;
    m_preview_mesh->clear_primitives(); // TODO This is dangerous, as primitives *must* first be removed from rt_scene
    m_preview_mesh->add_primitive(brush_scaled.primitive, material);

    const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
    if (hover_scene_mesh) {
        m_preview_node->set_parent(hover_scene_mesh->get_node());
        m_preview_node->set_parent_from_node(transform);
    } else if (hover_grid) {
        ERHE_VERIFY(scene_root);
        m_preview_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
        m_preview_node->set_parent_from_node(transform);
    }
    m_preview_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
}

void Brush_tool::do_insert_operation(Brush& brush)
{
    const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
    if (!scene_root) {
        return;
    }

    const std::shared_ptr<erhe::primitive::Material>& material = get_material();
    if (!m_hover.position.has_value() || !material) {
        return;
    }

    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        hover_grid       = m_hover.grid_weak.lock();

    if (!m_align_transform.has_value()) {
        return;
    }
    const auto hover_from_brush = m_align_transform.value();
    const uint64_t mesh_flags =
        erhe::Item_flags::visible     |
        erhe::Item_flags::content     |
        erhe::Item_flags::opaque      |
        erhe::Item_flags::shadow_cast |
        erhe::Item_flags::id          |
        erhe::Item_flags::show_in_ui;
    const uint64_t node_flags =
        erhe::Item_flags::visible     |
        erhe::Item_flags::content     |
        erhe::Item_flags::show_in_ui;

    auto* const hover_node = hover_scene_mesh ? hover_scene_mesh->get_node() : nullptr;
    const Instance_create_info brush_instance_create_info {
        .node_flags       = node_flags,
        .mesh_flags       = mesh_flags,
        .scene_root       = scene_root.get(),
        .world_from_node  = (hover_node != nullptr)
            ? hover_node->world_from_node() * hover_from_brush
            : hover_from_brush,
        .material         = material,
        .scale            = m_transform_scale
    };
    const auto instance_node = brush.make_instance(brush_instance_create_info);

    std::shared_ptr<Brush> shared_brush = std::dynamic_pointer_cast<Brush>(brush.shared_from_this());
    std::shared_ptr<Brush_placement> brush_placement = std::make_shared<Brush_placement>(shared_brush, get_placement_facet(), get_placement_corner0());
    instance_node->attach(brush_placement);
    brush_placement->enable_flag_bits(
        erhe::Item_flags::brush      |
        erhe::Item_flags::visible    |
        erhe::Item_flags::no_message |
        erhe::Item_flags::show_in_ui
    );

    std::shared_ptr<erhe::scene::Node> parent;
    const auto& first_selected_node = m_context.selection->get_last_selected<erhe::scene::Node>();

    if (m_parent_to_first_selected && first_selected_node && (first_selected_node->get_item_host() != nullptr)) {
        parent = first_selected_node;
    }
    erhe::scene::Scene&                       scene     = scene_root->get_scene();
    const std::shared_ptr<erhe::scene::Node>& root_node = scene.get_root_node();
    if (!parent && m_parent_to_scene_root) {
        if (root_node) {
            parent = root_node;
        }
    }
    if (!parent && m_parent_to_hovered) {
        if (hover_node) {
            parent = std::static_pointer_cast<erhe::scene::Node>(hover_node->shared_from_this());
        } else if (root_node) {
            parent = root_node;
        }
    }

    auto op = std::make_shared<Item_insert_remove_operation>(
        Item_insert_remove_operation::Parameters{
            .context = m_context,
            .item    = instance_node,
            .parent  = parent,
            .mode    = Item_insert_remove_operation::Mode::insert
        }
    );
    m_context.operation_stack->queue(op);
}

void Brush_tool::add_preview_mesh(Brush& brush)
{
    const auto material = get_material();
    auto* scene_view = get_hover_scene_view();

    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        hover_grid       = m_hover.grid_weak.lock();

    if (!material || !m_hover.position.has_value() || (!hover_scene_mesh && !hover_grid) || (scene_view == nullptr)) {
        return;
    }

    const auto& scene_root = scene_view->get_scene_root();
    ERHE_VERIFY(scene_root);

    brush.late_initialize();
    const auto& brush_scaled = brush.get_scaled(m_transform_scale);
    const std::string name = fmt::format("brush-{}", brush.get_name());
    m_preview_node = std::make_shared<erhe::scene::Node>(name);
    m_preview_mesh = std::make_shared<erhe::scene::Mesh>(name);
    m_preview_mesh->add_primitive(brush_scaled.primitive, material);
    m_preview_node->enable_flag_bits(
        erhe::Item_flags::brush      |
        erhe::Item_flags::visible    |
        erhe::Item_flags::no_message |
        (m_context.developer_mode ? erhe::Item_flags::show_in_ui : 0)
    );
    m_preview_mesh->enable_flag_bits(
        erhe::Item_flags::brush       |
        erhe::Item_flags::visible     |
        erhe::Item_flags::translucent | // redundant
        erhe::Item_flags::no_message  |
        (m_context.developer_mode ? erhe::Item_flags::show_in_ui : 0)
    );

    m_preview_mesh->layer_id = scene_root->layers().brush()->id;

    m_preview_node->attach(m_preview_mesh);
    m_preview_node->set_parent(scene_root->get_scene().get_root_node());

    update_preview_mesh_node_transform();
}

void Brush_tool::update_preview_mesh()
{
    const auto brush = m_drag_and_drop_brush ? m_drag_and_drop_brush : m_context.selection->get_last_selected<Brush>();

    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        hover_grid       = m_hover.grid_weak.lock();

    if (!m_preview_mesh && (is_enabled() || m_drag_and_drop_brush) && m_show_preview) {
        if (!brush || !m_hover.position.has_value() || (!hover_scene_mesh && !hover_grid)) {
            return;
        }

        add_preview_mesh(*brush.get());
        return;
    }

    if (
        (!m_drag_and_drop_brush && !is_enabled()) ||
        !brush ||
        !m_hover.position.has_value() ||
        !m_show_preview ||
        (!hover_scene_mesh && !hover_grid)
    ) {
        remove_preview_mesh();
    }

    update_preview_mesh_node_transform();
}

void Brush_tool::tool_properties(erhe::imgui::Imgui_window& imgui_window)
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Brush> last_selected_brush = m_context.selection->get_last_selected<Brush>();
    const std::shared_ptr<Brush> hover_brush = get_hover_brush();

    ImGui::Checkbox("Parent to Selected", &m_parent_to_first_selected);
    ImGui::Checkbox("Parent to Scene",    &m_parent_to_scene_root    );
    ImGui::Checkbox("Parent to Hovered",  &m_parent_to_hovered       );

    ImGui::Text("Brush: %s", last_selected_brush ? last_selected_brush->get_name().c_str() : "");
    //ImGui::Text("Hover Brush: %s", hover_brush ? hover_brush->get_name().c_str() : "");
    //ImGui::Text("Drag and Drop Brush: %s", m_drag_and_drop_brush ? m_drag_and_drop_brush->get_name().c_str() : "");
    ImGui::SliderFloat("Preview Hover", &m_preview_hover_distance, -0.1f, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
    //ImGui::Text("Enabled: %s", is_enabled() ? "yes" : "no");
    //if (m_hover.position.has_value()) {
    //    const glm::vec3& p = m_hover.position.value();
    //    ImGui::Text("Hover position: %.2f, %.2f, %.2f", p.x, p.y, p.z);
    //} else {
    //    ImGui::Text("Hover position: -");
    //}
    //ImGui::Text("Hover mesh: %s", m_hover.mesh ? m_hover.mesh->get_name().c_str() : "");
    //ImGui::Text("Hover grid: %s", m_hover.grid ? m_hover.grid->get_name().c_str() : "");

    const auto brush = m_context.selection->get_last_selected<Brush>();
    if (brush && ImGui::TreeNodeEx(brush->get_name().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        std::shared_ptr<erhe::geometry::Geometry> geometry = brush->get_geometry();
        if (geometry) {
            const std::map<GEO::index_t, std::vector<GEO::index_t>>& facets = brush->get_corner_count_to_facets();
            for (const auto& i : facets) {
                bool selected = m_selected_corner_count.has_value() && m_selected_corner_count.value() == i.first;
                std::string label = fmt::format("{}-gons: {}", i.first, i.second.size());
                if (ImGui::Selectable(label.c_str(), &selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    if (selected) {
                        m_selected_corner_count = i.first;
                    } else {
                        m_selected_corner_count.reset();
                    }
                }
            }
        }
        ImGui::TreePop();
    }
    const auto material = m_context.selection->get_last_selected<erhe::primitive::Material>();
    if (material) {
        ImGui::Text("Material: %s", material->get_name().c_str());
    }

    if (ImGui::Checkbox("Scale to Match", &m_scale_to_match) && !m_scale_to_match) {
        m_transform_scale = 1.0f;
    }

    if (ImGui::RadioButton("Use Matching Face", m_use_matching_face)) {
        m_use_matching_face = true;
        m_use_selected_face = false;
    }
    if (ImGui::RadioButton("Use Selected Face", m_use_selected_face)) {
        m_use_matching_face = false;
        m_use_selected_face = true;
    }

    ImGui::Checkbox   ("Snap to Polygon", &m_snap_to_hover_polygon);
    ImGui::Checkbox   ("Snap To Grid",    &m_snap_to_grid);
    ImGui::SliderFloat("Scale",           &m_scale, 0.0001f, 32.0f, "%.3f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Grid Scale",      &m_grid_scale, 0.0001f, 32.0f, "%.3f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox   ("Physics",         &m_with_physics);
    ImGui::DragInt    ("Face Offset",     &m_polygon_offset, 0.1f, 0, INT_MAX);
    ImGui::DragInt    ("Corner Offset",   &m_corner_offset,  0.1f, 0, INT_MAX);

    ImGui::NewLine    ();
    ImGui::Checkbox   ("Debug Visualization", &m_debug_visualization);
    ImGui::Checkbox   ("Show Receiver Frame", &m_show_receiver_frame);
    ImGui::Checkbox   ("Show Brush Frame",    &m_show_brush_frame);
    ImGui::Checkbox   ("Show Preview",        &m_show_preview);

    if (ImGui::IsItemEdited()) {
        if (m_show_preview) {
            update_preview_mesh();
        }
        else {
            remove_preview_mesh();
        }
    }

    if (!m_brush_item_tree) {
        std::shared_ptr<Content_library> content_library = get_content_library();
        if (content_library) {
            m_brush_item_tree = std::make_unique<Item_tree>(m_context);
            m_brush_item_tree->set_root(content_library->brushes);
            m_brush_item_tree->set_item_filter(
                erhe::Item_filter{
                    .require_all_bits_set           = 0,
                    .require_at_least_one_bit_set   = 0,
                    .require_all_bits_clear         = 0,
                    .require_at_least_one_bit_clear = 0
                }
            );
        }
    }
    if (m_brush_item_tree) {
        m_brush_item_tree->imgui_tree(imgui_window.get_scale_value());
    }
}

void Brush_tool::tool_render(const Render_context& render_context)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_debug_visualization) {
        return;
    }

    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        hover_grid       = m_hover.grid_weak.lock();

    auto shared_brush = m_drag_and_drop_brush ? m_drag_and_drop_brush : m_context.selection->get_last_selected<Brush>();
    //if (
    //    !shared_brush ||
    //    !m_hover.position.has_value() ||
    //    (!hover_scene_mesh && !hover_grid)
    //) {
    //    return;
    //}

    if (m_hover_frame.has_value()) {
        const Reference_frame& hover_frame      = m_hover_frame.value();
        const GEO::mat4f       hover_transform_ = hover_frame.transform(0.0f);
        const glm::mat4        hover_transform  = to_glm_mat4(hover_transform_);
        const glm::mat4        world_from_hover = m_world_from_hover.has_value() ? m_world_from_hover.value() : glm::mat4{1.0f};
        const glm::mat4        world_from_align = world_from_hover * hover_transform;
        constexpr glm::vec3 C    {0.0f, 0.0f, 0.0f};
        constexpr glm::vec3 C_t  {1.0f, 0.0f, 0.0f};
        constexpr glm::vec3 C_b  {0.0f, 1.0f, 0.0f};
        constexpr glm::vec3 C_n  {0.0f, 0.0f, 1.0f};
        constexpr glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
        constexpr glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
        constexpr glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
        erhe::renderer::Primitive_renderer line_renderer = render_context.get_line_renderer(2, true, true);
        line_renderer.set_thickness(10.0f);
        line_renderer.add_lines( world_from_align, red,   { { C, C_t }});
        line_renderer.add_lines( world_from_align, green, { { C, C_b }});
        line_renderer.add_lines( world_from_align, blue,  { { C, C_n }});
    }

#if 0
    if (m_show_receiver_frame) {
        //if (!hover_scene_mesh && hover_grid) {
        //    const glm::mat4 world_from_hover = get_world_from_grid_hover_point();
        //    constexpr glm::vec3 C    {0.0f, 0.0f, 0.0f};
        //    constexpr glm::vec3 C_t  {1.0f, 0.0f, 0.0f};
        //    constexpr glm::vec3 C_b  {0.0f, 1.0f, 0.0f};
        //    constexpr glm::vec3 C_n  {0.0f, 0.0f, 1.0f};
        //    constexpr glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
        //    constexpr glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
        //    constexpr glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
        //    erhe::renderer::Primitive_renderer line_renderer = render_context.get_line_renderer(2, true, true);
        //    line_renderer.set_thickness(10.0f);
        //    line_renderer.add_lines( world_from_hover, red,   { { C, C_t }});
        //    line_renderer.add_lines( world_from_hover, green, { { C, C_b }});
        //    line_renderer.add_lines( world_from_hover, blue,  { { C, C_n }});
        //    return;
        //}
        //
        //erhe::scene::Node* hover_node = hover_scene_mesh->get_node();
        //if (hover_node == nullptr) {
        //    return;
        //}
        //
        //const glm::mat4 world_from_hover_node = hover_node->world_from_node();
        //
        //Brush& brush = *shared_brush.get();
        //auto* geometry = m_hover.geometry.get();
        //const GEO::Mesh& geo_mesh = geometry->get_mesh();
        //GEO::index_t corner = 0;
        //Reference_frame hover_frame{geo_mesh, m_hover.facet, 0, corner, Frame_orientation::out};
        //const GEO::index_t hover_facet_corner_count = geo_mesh.facets.nb_corners(m_hover.facet);
        //m_brush_placement_frame = brush.get_reference_frame(
        //    hover_facet_corner_count,
        //    static_cast<uint32_t>(m_polygon_offset),
        //    static_cast<uint32_t>(m_corner_offset)
        //);

        const GEO::vec3f C_   = hover_frame.m_centroid;
        const GEO::vec3f C_t_ = hover_frame.m_centroid + hover_frame.m_T;
        const GEO::vec3f C_b_ = hover_frame.m_centroid + hover_frame.m_B;
        const GEO::vec3f C_n_ = hover_frame.m_centroid + hover_frame.m_N;
        const glm::vec3  C    = to_glm_vec3(C_  );
        const glm::vec3  C_t  = to_glm_vec3(C_t_);
        const glm::vec3  C_b  = to_glm_vec3(C_b_);
        const glm::vec3  C_n  = to_glm_vec3(C_n_);
        constexpr glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
        constexpr glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
        constexpr glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
        erhe::renderer::Primitive_renderer line_renderer = render_context.get_line_renderer(2, true, true);
        line_renderer.set_thickness(10.0f);
        line_renderer.add_lines( world_from_hover_node, red,   { { C, C_t }});
        line_renderer.add_lines( world_from_hover_node, green, { { C, C_b }});
        line_renderer.add_lines( world_from_hover_node, blue,  { { C, C_n }});
    }
#endif
}

}
