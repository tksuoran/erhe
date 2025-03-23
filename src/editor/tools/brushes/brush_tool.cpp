#include "tools/brushes/brush_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/render_context.hpp"
#include "scene/brush_placement.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/brushes/brush.hpp"
#include "tools/grid.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"
#include "windows/property_editor.hpp"
#include "windows/item_tree_window.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_renderer/line_renderer.hpp"
#include "erhe_renderer/scoped_line_renderer.hpp"
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

#include <geogram/basic/matrix.h>

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace editor {


#pragma region Commands
Brush_tool_preview_command::Brush_tool_preview_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Brush_tool.motion_preview"}
    , m_context{context}
{
}

auto Brush_tool_preview_command::try_call() -> bool
{
    if ((get_command_state() != erhe::commands::State::Active) || !m_context.brush_tool->is_enabled()) {
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

Brush_tool_pick_command::Brush_tool_pick_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Brush_tool.pick"}
    , m_context{context}
{
}

auto Brush_tool_pick_command::try_call() -> bool
{
    return m_context.brush_tool->try_pick();
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
    : Tool                            {editor_context}
    , m_preview_command               {commands, editor_context}
    , m_insert_command                {commands, editor_context}
    , m_pick_command                  {commands, editor_context}
    , m_pick_using_float_input_command{commands, m_pick_command, 0.6f, 0.4f}
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority(Brush_tool::c_priority);
    set_description  ("Brush Tool");
    set_flags        (Tool_flags::toolbox);
    set_icon         (icon_set.icons.brush_big);

    commands.register_command(&m_preview_command);
    commands.register_command(&m_insert_command);
    commands.register_command(&m_pick_using_float_input_command);
    commands.bind_command_to_update      (&m_preview_command);
    commands.bind_command_to_mouse_button(&m_insert_command, erhe::window::Mouse_button_right,  true);
    commands.bind_command_to_mouse_button(&m_pick_command,   erhe::window::Mouse_button_middle, true);

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

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );

    m_preview_command               .set_host(this);
    m_insert_command                .set_host(this);
    m_pick_command                  .set_host(this);
    m_pick_using_float_input_command.set_host(this);
}

void Brush_tool::on_message(Editor_message& message)
{
    Scene_view* const old_scene_view = get_hover_scene_view();

    Tool::on_message(message);
    using namespace erhe::bit;
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
        disable();
        m_preview_command.set_inactive();
        m_hover = Hover_entry{};
        remove_preview_mesh();
    }
    if (new_priority > old_priority) {
        enable();
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
}

auto Brush_tool::try_insert_ready() -> bool
{
    return is_enabled() && ((m_hover.scene_mesh != nullptr) || (m_hover.grid != nullptr));
}

auto Brush_tool::get_hover_brush() const -> std::shared_ptr<Brush>
{
    if (m_hover.scene_mesh == nullptr) {
        return {};
    }
    erhe::scene::Node* node = m_hover.scene_mesh->get_node();
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
        m_context.editor_scenes->sanity_check();
#endif

    do_insert_operation(*brush);

#if !defined(NDEBUG)
        m_context.editor_scenes->sanity_check();
#endif

    remove_preview_mesh();

#if !defined(NDEBUG)
        m_context.editor_scenes->sanity_check();
#endif

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

    if ((m_hover.scene_mesh != nullptr) && (m_hover.scene_mesh->get_node() != nullptr) && m_hover.position.has_value()) {
        m_hover.position = m_hover.scene_mesh->get_node()->transform_direction_from_world_to_local(
            m_hover.position.value()
        );
    }

    update_preview_mesh();
}

void Brush_tool::preview_drag_and_drop(std::shared_ptr<Brush> brush)
{
    m_drag_and_drop_brush = brush;
    on_motion();
}

auto Brush_tool::get_placement_facet() const -> GEO::index_t
{
    return m_brush_placement_frame.m_facet;
}

auto Brush_tool::get_placement_corner() const -> GEO::index_t
{
    return m_brush_placement_frame.m_corner;
}

// Returns transform which places brush in parent (hover) mesh space.
auto Brush_tool::get_hover_mesh_transform(Brush& brush, const float hover_distance) -> glm::mat4
{
    if (
        (m_hover.scene_mesh == nullptr)                                            ||
        (m_hover.scene_mesh_primitive_index == std::numeric_limits<std::size_t>::max()) ||
        (!m_hover.geometry)
    ) {
        return glm::mat4{1};
    }
    auto* geometry = m_hover.geometry.get();
    const GEO::Mesh& geo_mesh = geometry->get_mesh();
    ERHE_VERIFY(m_hover.facet < geo_mesh.facets.nb());

    Reference_frame hover_frame{geo_mesh, m_hover.facet, 0, 0};
    const GEO::index_t hover_facet_corner_count = geo_mesh.facets.nb_corners(m_hover.facet);

    m_brush_placement_frame = Reference_frame{};
    if (m_use_matching_face) {
        m_brush_placement_frame = brush.get_reference_frame(
            hover_facet_corner_count,
            static_cast<uint32_t>(m_polygon_offset),
            static_cast<uint32_t>(m_corner_offset)
        );
        hover_frame.m_N *= -1.0f;
        hover_frame.m_B *= -1.0f;
    } else if (m_use_selected_face) {
        const size_t selected_corner_count = m_selected_corner_count.has_value() ? m_selected_corner_count.value() : brush.get_max_corner_count();
        m_brush_placement_frame = brush.get_reference_frame(
            static_cast<uint32_t>(selected_corner_count),
            static_cast<uint32_t>(m_polygon_offset),
            static_cast<uint32_t>(m_corner_offset)
        );
        hover_frame.m_N *= -1.0f;
        hover_frame.m_B *= -1.0f;
    }

    ERHE_VERIFY(m_brush_placement_frame.scale() != 0.0f);

    if (m_scale_to_match) {
        const float hover_scale = hover_frame.scale();
        const float brush_scale = m_brush_placement_frame.scale();
        const float scale       = (brush_scale != 0.0f) ? static_cast<float>(hover_scale / brush_scale) : 1.0f;

        m_transform_scale = m_scale * scale;
        if (m_transform_scale != 1.0f) {
            const glm::mat4 scale_transform = erhe::math::create_scale(m_transform_scale);
            m_brush_placement_frame.transform_by(to_geo_mat4f(scale_transform));
        }
    }

    if (!m_snap_to_hover_polygon && m_hover.position.has_value()) {
        hover_frame.m_centroid = to_geo_vec3f(m_hover.position.value());
    }

    const GEO::mat4f hover_transform = hover_frame.transform(0.0f);
    const GEO::mat4f brush_transform = m_brush_placement_frame.transform(hover_distance);
    const GEO::mat4f inverse_brush   = brush_transform.inverse();
    const GEO::mat4f align           = hover_transform * inverse_brush;
    return to_glm_mat4(align);
}

auto Brush_tool::get_hover_grid_transform(Brush& brush, const float hover_distance) -> glm::mat4
{
    m_transform_scale = m_scale;
    m_brush_placement_frame = brush.get_reference_frame(
        static_cast<uint32_t>(m_selected_corner_count.has_value() ? m_selected_corner_count.value() : brush.get_max_corner_count()),
        static_cast<uint32_t>(m_polygon_offset),
        static_cast<uint32_t>(m_corner_offset)
    );
    const glm::mat4  scale_transform = erhe::math::create_scale(m_scale);
    m_brush_placement_frame.transform_by(to_geo_mat4f(scale_transform));
    const GEO::mat4f brush_transform = m_brush_placement_frame.transform(hover_distance);
    const GEO::mat4f inverse_brush   = brush_transform.inverse();

    if (m_hover.grid == nullptr) {
        return glm::mat4{1};
    }

    const glm::vec3 position_in_grid0 = m_hover.grid->grid_from_world() * glm::vec4{m_hover.position.value(), 1.0f};
    const glm::vec3 position_in_grid  = m_snap_to_grid ? m_hover.grid->snap_grid_position(position_in_grid0) : position_in_grid0;
    const glm::mat4 offset            = erhe::math::create_translation<float>(position_in_grid);
    const glm::mat4 world_from_grid   = m_hover.grid->world_from_grid() * offset;

    const glm::mat4 align = glm::mat4{world_from_grid} * to_glm_mat4(inverse_brush);
    return align;
}

void Brush_tool::update_preview_mesh_node_transform()
{
    auto shared_brush = m_drag_and_drop_brush ? m_drag_and_drop_brush : m_context.selection->get_last_selected<Brush>();
    if (
        !shared_brush ||
        !m_hover.position.has_value() ||
        !m_preview_mesh ||
        !m_preview_node ||
        ((m_hover.scene_mesh == nullptr) && (m_hover.grid == nullptr))
    ) {
        return;
    }

    Brush& brush = *shared_brush.get();
    const auto transform = (m_hover.scene_mesh != nullptr)
        ? get_hover_mesh_transform(brush, m_preview_hover_distance) 
        : get_hover_grid_transform(brush, m_preview_hover_distance);
    const auto& brush_scaled = brush.get_scaled(m_transform_scale);

    // TODO Unparent, to remove raytrace primitives to raytrace scene.
    //      This is a workaround for clear_primitives() issue below
    m_preview_node->set_parent({});

    const std::shared_ptr<erhe::primitive::Primitive_render_shape> shape = m_preview_mesh->get_primitives().front().render_shape;
    ERHE_VERIFY(shape);

    // TODO Brush mesh does not really need RT primitives at all

    const std::shared_ptr<erhe::primitive::Material>& material = m_preview_mesh->get_primitives().front().material;
    m_preview_mesh->clear_primitives(); // TODO This is dangerous, as primitives *must* first be removed from rt_scene
    m_preview_mesh->add_primitive(brush_scaled.primitive, material);

    if (m_hover.scene_mesh != nullptr) {
        m_preview_node->set_parent(m_hover.scene_mesh->get_node());
        m_preview_node->set_parent_from_node(transform);
    } else if (m_hover.grid) {
        auto* scene_view = get_hover_scene_view();
        ERHE_VERIFY(scene_view != nullptr);
        const auto& scene_root = scene_view->get_scene_root();
        ERHE_VERIFY(scene_root);
        m_preview_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
        m_preview_node->set_parent_from_node(transform);
    }
}

void Brush_tool::do_insert_operation(Brush& brush)
{
    const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
    if (!scene_root) {
        return;
    }

    //// const std::shared_ptr<Brush>& brush = m_context.selection->get_last_selected<Brush>();
    const std::shared_ptr<erhe::primitive::Material>& material = get_material();
    if (!m_hover.position.has_value() || !material) {
        return;
    }

    const auto hover_from_brush = (m_hover.scene_mesh != nullptr) ? get_hover_mesh_transform(brush, 0.0f) : get_hover_grid_transform(brush, 0.0f);
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

    auto* const hover_node = (m_hover.scene_mesh != nullptr) ? m_hover.scene_mesh->get_node() : nullptr;
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
    std::shared_ptr<Brush_placement> brush_placement = std::make_shared<Brush_placement>(shared_brush, get_placement_facet(), get_placement_corner());
    instance_node->attach(brush_placement);

    std::shared_ptr<erhe::scene::Node> parent = (hover_node != nullptr)
        ? std::static_pointer_cast<erhe::scene::Node>(hover_node->shared_from_this())
        : scene_root->get_hosted_scene()->get_root_node();
    const auto& first_selected_node = m_context.selection->get_last_selected<erhe::scene::Node>();
    // If item host is nullptr, the item is not in scene, and it is probably in undo stack.
    // In that case, this is not the item user is looking for.
    if (first_selected_node && (first_selected_node->get_item_host() != nullptr)) {
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

void Brush_tool::add_preview_mesh(Brush& brush)
{
    const auto material = get_material();
    auto* scene_view = get_hover_scene_view();
    if (!material || !m_hover.position.has_value() || ((m_hover.scene_mesh == nullptr) && (m_hover.grid == nullptr)) || (scene_view == nullptr)) {
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
    if (!m_preview_mesh && (is_enabled() || m_drag_and_drop_brush)) {
        if (!brush || !m_hover.position.has_value() || ((m_hover.scene_mesh == nullptr) && (m_hover.grid == nullptr))) {
            return;
        }

        add_preview_mesh(*brush.get());
        return;
    }

    if (
        (!m_drag_and_drop_brush && !is_enabled()) ||
        !brush ||
        !m_hover.position.has_value() ||
        (
            (m_hover.scene_mesh == nullptr) && (m_hover.grid == nullptr)
        )
    ) {
        remove_preview_mesh();
    }

    update_preview_mesh_node_transform();
}

void Brush_tool::tool_properties(erhe::imgui::Imgui_window& imgui_window)
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Brush> last_selected_brush = m_context.selection->get_last_selected<Brush>();
    const std::shared_ptr<Brush> hover_brush = get_hover_brush();
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
    ImGui::Checkbox   ("Physics",         &m_with_physics);
    ImGui::DragInt    ("Face Offset",     &m_polygon_offset, 0.1f, 0, INT_MAX);
    ImGui::DragInt    ("Corner Offset",   &m_corner_offset,  0.1f, 0, INT_MAX);

    ImGui::NewLine    ();
    ImGui::Checkbox   ("Debug visualization", &m_debug_visualization);

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
#endif
}

void Brush_tool::tool_render(const Render_context& render_context)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_debug_visualization) {
        return;
    }

    auto shared_brush = m_drag_and_drop_brush ? m_drag_and_drop_brush : m_context.selection->get_last_selected<Brush>();
    if (
        !shared_brush ||
        !m_hover.position.has_value() ||
        (m_hover.scene_mesh == nullptr)
    ) {
        return;
    }

    erhe::scene::Node* hover_node = m_hover.scene_mesh->get_node();
    if (hover_node == nullptr) {
        return;
    }
    const glm::mat4 world_from_hover_node = hover_node->world_from_node();

    Brush& brush = *shared_brush.get();
    auto* geometry = m_hover.geometry.get();
    const GEO::Mesh& geo_mesh = geometry->get_mesh();
    Reference_frame hover_frame{geo_mesh, m_hover.facet, 0, 0};
    const GEO::index_t hover_facet_corner_count = geo_mesh.facets.nb_corners(m_hover.facet);
    m_brush_placement_frame = brush.get_reference_frame(
        hover_facet_corner_count,
        static_cast<uint32_t>(m_polygon_offset),
        static_cast<uint32_t>(m_corner_offset)
    );
    hover_frame.m_N *= -1.0f;
    hover_frame.m_B *= -1.0f;

    const GEO::vec3f C_   = hover_frame.m_centroid;
    const GEO::vec3f C_n_ = hover_frame.m_centroid + hover_frame.m_N;
    const GEO::vec3f C_t_ = hover_frame.m_centroid + hover_frame.m_T;
    const GEO::vec3f C_b_ = hover_frame.m_centroid + hover_frame.m_B;
    const glm::vec3  C    = to_glm_vec3(C_  );
    const glm::vec3  C_n  = to_glm_vec3(C_n_);
    const glm::vec3  C_t  = to_glm_vec3(C_t_);
    const glm::vec3  C_b  = to_glm_vec3(C_b_);
    constexpr glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
    constexpr glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
    erhe::renderer::Scoped_line_renderer line_renderer = render_context.get_line_renderer(2, true, true);
    line_renderer.set_thickness(10.0f);
    line_renderer.add_lines( world_from_hover_node, red,   { { C, C_n }});
    line_renderer.add_lines( world_from_hover_node, green, { { C, C_t }});
    line_renderer.add_lines( world_from_hover_node, blue,  { { C, C_b }});
}

} // namespace editor
