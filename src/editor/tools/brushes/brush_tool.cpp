
#include "tools/brushes/brush_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/brushes/brush.hpp"
#include "tools/grid.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_renderer/line_renderer.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace editor {

using glm::mat3;
using glm::mat4;
using glm::vec3;
using glm::vec4;
using erhe::geometry::Polygon; // Resolve conflict with wingdi.h BOOL Polygon(HDC,const POINT *,int)

#pragma region Commands
Brush_tool_preview_command::Brush_tool_preview_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Brush_tool.motion_preview"}
    , m_context{context}
{
}

auto Brush_tool_preview_command::try_call() -> bool
{
    if (
        (get_command_state() != erhe::commands::State::Active) ||
        !m_context.brush_tool->is_enabled()
    ) {
        return false;
    }
    m_context.brush_tool->on_motion();
    return true;
}

Brush_tool_insert_command::Brush_tool_insert_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Brush_tool.insert"}
    , m_context{context}
{
}

void Brush_tool_insert_command::try_ready()
{
    if (m_context.brush_tool->try_insert_ready()) {
        set_ready();
    }
}

auto Brush_tool_insert_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        return false;
    }
    const bool consumed = m_context.brush_tool->try_insert();
    set_inactive();
    return consumed;
}
#pragma endregion Commands

Brush_tool::Brush_tool(
    erhe::commands::Commands& commands,
    Editor_context&           editor_context,
    Editor_message_bus&       editor_message_bus,
    Headset_view&             headset_view,
    Icon_set&                 icon_set,
    Tools&                    tools
)
    : Tool             {editor_context}
    , m_preview_command{commands, editor_context}
    , m_insert_command {commands, editor_context}
{
    set_base_priority(Brush_tool::c_priority);
    set_description  ("Brush Tool");
    set_flags        (Tool_flags::toolbox);
    set_icon         (icon_set.icons.brush_big);

    commands.register_command(&m_preview_command);
    commands.register_command(&m_insert_command);
    commands.bind_command_to_update      (&m_preview_command);
    commands.bind_command_to_mouse_button(&m_insert_command, erhe::window::Mouse_button_right, true);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = headset_view.get_headset();
    if (headset != nullptr) {
        auto& xr_right = headset->get_actions_right();
        commands.bind_command_to_xr_boolean_action(&m_insert_command, xr_right.trigger_click, erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_insert_command, xr_right.a_click,       erhe::commands::Button_trigger::Button_pressed);
    }
#else
    static_cast<void>(headset_view);
#endif

    tools.register_tool(this);

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );

    m_preview_command.set_host(this);
    m_insert_command .set_host(this);
}

void Brush_tool::on_message(Editor_message& message)
{
    Tool::on_message(message);
    using namespace erhe::bit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_mesh)) {
        on_motion();
    }
}

void Brush_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (new_priority < old_priority) {
        disable();
        m_preview_command.set_inactive();
        m_hover = Hover_entry{};
        remove_brush_mesh();
    }
    if (new_priority > old_priority) {
        enable();
        m_preview_command.set_active();
        on_motion();
    }
}

void Brush_tool::remove_brush_mesh()
{
    // Remove mesh attachment *before* removing node
    if (m_brush_mesh) {
        m_brush_mesh.reset();
    }
    if (m_brush_node) {
        // Do not remove this. It is not possible to rely on reset calling destructor,
        // because parent will have shared_ptr to the child.
        m_brush_node->set_parent({});
        m_brush_node.reset();
    }
}

auto Brush_tool::try_insert_ready() -> bool
{
    return is_enabled() && (m_hover.mesh || (m_hover.grid != nullptr));
}

auto Brush_tool::try_insert() -> bool
{
    auto brush = m_context.selection->get_last_selected<Brush>();
    if (!m_brush_node || !m_brush_mesh || !m_hover.position.has_value() || !brush) {
        return false;
    }

        m_context.editor_scenes->sanity_check();

    do_insert_operation();

        m_context.editor_scenes->sanity_check();

    remove_brush_mesh();

        m_context.editor_scenes->sanity_check();

    return true;
}

void Brush_tool::on_motion()
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    const Hover_entry* nearest_hover = scene_view->get_nearest_hover(Hover_entry::content_bit | Hover_entry::grid_bit);
    if (nearest_hover == nullptr) {
        return;
    }
    m_hover = *nearest_hover;

    if (
        m_hover.mesh &&
        (m_hover.mesh->get_node() != nullptr) &&
        m_hover.position.has_value()
    ) {
        m_hover.position = m_hover.mesh->get_node()->transform_direction_from_world_to_local(
            m_hover.position.value()
        );
    }

    update_mesh();
}

// Returns transform which places brush in parent (hover) mesh space.
auto Brush_tool::get_hover_mesh_transform() -> mat4
{
    auto brush = m_context.selection->get_last_selected<Brush>();

    if (
        (m_hover.mesh == nullptr)                                            ||
        (m_hover.primitive_index == std::numeric_limits<std::size_t>::max()) ||
        (!m_hover.geometry)
    ) {
        return mat4{1};
    }
    auto* geometry = m_hover.geometry.get();
    ERHE_VERIFY(m_hover.polygon_id < geometry->get_polygon_count());

    const auto polygon_id = static_cast<erhe::geometry::Polygon_id>(m_hover.polygon_id);
    const Polygon&  polygon    = geometry->polygons[polygon_id];
    Reference_frame hover_frame(*geometry, polygon_id, 0, 0);

    Reference_frame brush_frame = brush->get_reference_frame(
        polygon.corner_count,
        static_cast<uint32_t>(m_polygon_offset),
        static_cast<uint32_t>(m_corner_offset)
    );
    hover_frame.N *= -1.0f;
    hover_frame.B *= -1.0f;

    ERHE_VERIFY(brush_frame.scale() != 0.0f);

    const float hover_scale = hover_frame.scale();
    const float brush_scale = brush_frame.scale();
    const float scale       = hover_scale / brush_scale;

    m_transform_scale = m_scale * scale;
    if (m_transform_scale != 1.0f) {
        const mat4 scale_transform = erhe::math::create_scale(m_transform_scale);
        brush_frame.transform_by(scale_transform);
    }

    if (
        !m_snap_to_hover_polygon &&
        m_hover.position.has_value()
    ) {
        hover_frame.centroid = m_hover.position.value();
    }

    const mat4 hover_transform = hover_frame.transform();
    const mat4 brush_transform = brush_frame.transform();
    const mat4 inverse_brush   = inverse(brush_transform);
    const mat4 align           = hover_transform * inverse_brush;
    return align;
}

auto Brush_tool::get_hover_grid_transform() -> mat4
{
    if (m_hover.grid == nullptr) {
        return mat4{1};
    }

    m_transform_scale = m_scale;
    auto brush = m_context.selection->get_last_selected<Brush>();
    Reference_frame brush_frame = brush->get_reference_frame(
        static_cast<uint32_t>(m_polygon_offset),
        static_cast<uint32_t>(m_corner_offset)
    );
    const mat4 scale_transform = erhe::math::create_scale(m_scale);
    brush_frame.transform_by(scale_transform);
    const mat4 brush_transform = brush_frame.transform();
    const mat4 inverse_brush   = inverse(brush_transform);

    const glm::vec3 position_in_grid0 = m_hover.grid->grid_from_world() * glm::vec4{m_hover.position.value(), 1.0f};
    const glm::vec3 position_in_grid  = m_snap_to_grid ? m_hover.grid->snap_grid_position(position_in_grid0) : position_in_grid0;
    const glm::mat4 offset            = erhe::math::create_translation<float>(position_in_grid);
    const glm::mat4 world_from_grid   = m_hover.grid->world_from_grid() * offset;

    const mat4 align = mat4{world_from_grid} * inverse_brush;
    return align;
}

void Brush_tool::update_mesh_node_transform()
{
    auto brush = m_context.selection->get_last_selected<Brush>();
    if (
        !brush ||
        !m_hover.position.has_value() ||
        !m_brush_mesh ||
        !m_brush_node ||
        (!m_hover.mesh && (m_hover.grid == nullptr))
    ) {
        return;
    }

    const auto  transform    = m_hover.mesh ? get_hover_mesh_transform() : get_hover_grid_transform();
    const auto& brush_scaled = brush->get_scaled(m_transform_scale);

    // TODO Unparent, to remove raytrace primitives to raytrace scene.
    //      This is a workaround for clear_primitives() issue below
    m_brush_node->set_parent({});

    const std::shared_ptr<erhe::primitive::Primitive_render_shape> shape = m_brush_mesh->get_primitives().front().render_shape;
    ERHE_VERIFY(shape);

    // TODO Brush mesh does not really need RT primitives at all

    const std::shared_ptr<erhe::primitive::Material>& material = m_brush_mesh->get_primitives().front().material;
    m_brush_mesh->clear_primitives(); // TODO This is dangerous, as primitives *must* first be removed from rt_scene
    m_brush_mesh->add_primitive(brush_scaled.primitive, material);
    // m_brush_mesh->update_rt_primitives(); TODO this should no longer be needed

    if (m_hover.mesh) {
        m_brush_node->set_parent(m_hover.mesh->get_node());
        m_brush_node->set_parent_from_node(transform);
    } else if (m_hover.grid) {
        auto* scene_view = get_hover_scene_view();
        ERHE_VERIFY(scene_view != nullptr);
        const auto& scene_root = scene_view->get_scene_root();
        ERHE_VERIFY(scene_root);
        m_brush_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
        m_brush_node->set_parent_from_node(transform);
    }

}

void Brush_tool::do_insert_operation()
{
    const std::shared_ptr<Brush>& brush = m_context.selection->get_last_selected<Brush>();
    const std::shared_ptr<erhe::primitive::Material>& material = m_context.selection->get_last_selected<erhe::primitive::Material>();
    if (!m_hover.position.has_value() || !brush || !material) {
        return;
    }

    const auto hover_from_brush = m_hover.mesh ? get_hover_mesh_transform() : get_hover_grid_transform();
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

    auto* scene_view = get_hover_scene_view();
    ERHE_VERIFY(scene_view != nullptr);
    const auto& scene_root = scene_view->get_scene_root();
    ERHE_VERIFY(scene_root);

    auto* const hover_node = m_hover.mesh ? m_hover.mesh->get_node() : nullptr;
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
    const auto instance_node = brush->make_instance(brush_instance_create_info);

    std::shared_ptr<erhe::scene::Node> parent = (hover_node != nullptr)
        ? std::static_pointer_cast<erhe::scene::Node>(hover_node->shared_from_this())
        : scene_root->get_hosted_scene()->get_root_node();
    const auto& first_selected_node = m_context.selection->get_last_selected<erhe::scene::Node>();
    if (first_selected_node) {
        parent = first_selected_node;
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

void Brush_tool::add_brush_mesh()
{
    const auto brush    = m_context.selection->get_last_selected<Brush>();
    const auto material = m_context.selection->get_last_selected<erhe::primitive::Material>();
    auto* scene_view = get_hover_scene_view();
    if (
        !brush ||
        !material ||
        !m_hover.position.has_value() ||
        (!m_hover.mesh && (m_hover.grid == nullptr)) ||
        (scene_view == nullptr)
    ) {
        return;
    }

    const auto& scene_root = scene_view->get_scene_root();
    ERHE_VERIFY(scene_root);

    brush->late_initialize();
    const auto& brush_scaled = brush->get_scaled(m_transform_scale);
    const std::string name = fmt::format("brush-{}", brush->get_name());
    m_brush_node = std::make_shared<erhe::scene::Node>(name);
    m_brush_mesh = std::make_shared<erhe::scene::Mesh>(name);
    m_brush_mesh->add_primitive(brush_scaled.primitive, material);
    m_brush_node->enable_flag_bits(
        erhe::Item_flags::brush       |
        erhe::Item_flags::visible     |
        erhe::Item_flags::no_message
    );
    m_brush_mesh->enable_flag_bits(
        erhe::Item_flags::brush       |
        erhe::Item_flags::visible     |
        erhe::Item_flags::translucent | // redundant
        erhe::Item_flags::no_message
    );

    m_brush_mesh->layer_id = scene_root->layers().brush()->id;

    m_brush_node->attach(m_brush_mesh);
    m_brush_node->set_parent(scene_root->get_scene().get_root_node());

    update_mesh_node_transform();
}

void Brush_tool::update_mesh()
{
    const auto brush = m_context.selection->get_last_selected<Brush>();
    if (!m_brush_mesh && is_enabled()) {
        if (
            !brush ||
            !m_hover.position.has_value() ||
            (!m_hover.mesh && (m_hover.grid == nullptr))
        ) {
            return;
        }

        add_brush_mesh();
        return;
    }

    if (
        !is_enabled() ||
        !brush ||
        !m_hover.position.has_value() ||
        (!m_hover.mesh && (m_hover.grid == nullptr))
    ) {
        remove_brush_mesh();
    }

    update_mesh_node_transform();
}

void Brush_tool::tool_properties()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    ImGui::Checkbox   ("Snap to Polygon", &m_snap_to_hover_polygon);
    ImGui::Checkbox   ("Snap To Grid",    &m_snap_to_grid);
    ImGui::SliderFloat("Scale",           &m_scale, 0.0001f, 32.0f, "%.3f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox   ("Physics",         &m_with_physics);
    ImGui::DragInt    ("Face Offset",     &m_polygon_offset, 0.1f, 0, INT_MAX);
    ImGui::DragInt    ("Corner Offset",   &m_corner_offset,  0.1f, 0, INT_MAX);

    ImGui::NewLine    ();
    ImGui::Checkbox   ("Debug visualization", &m_debug_visualization);
#endif
}

void Brush_tool::tool_render(const Render_context&)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_debug_visualization || !m_brush_mesh || !m_brush_node) {
        return;
    }

    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

    const auto& transform = m_brush_node->parent_from_node_transform();
    glm::mat4 m = transform.get_matrix();

    constexpr vec3 O     { 0.0f };
    constexpr vec3 axis_x{ 1.0f, 0.0f, 0.0f};
    constexpr vec3 axis_y{ 0.0f, 1.0f, 0.0f};
    constexpr vec3 axis_z{ 0.0f, 0.0f, 1.0f};
    constexpr vec4 red   { 1.0f, 0.0f, 0.0f, 1.0f};
    constexpr vec4 green { 0.0f, 1.0f, 0.0f, 1.0f};
    constexpr vec4 blue  { 0.0f, 0.0f, 1.0f, 1.0f};

    line_renderer.set_thickness(10.0f);
    line_renderer.add_lines( m, red,   { { O, axis_x }});
    line_renderer.add_lines( m, green, { { O, axis_y }});
    line_renderer.add_lines( m, blue,  { { O, axis_z }});
}

} // namespace editor
