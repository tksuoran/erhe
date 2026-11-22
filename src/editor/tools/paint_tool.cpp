// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "tools/paint_tool.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "config/generated/paint_brush_blend.hpp"
#include "config/generated/paint_brush_channel.hpp"
#include "config/generated/paint_brush_config.hpp"
#include "config/generated/paint_brush_target.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "operations/compound_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/paint_colors_operation.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "windows/viewport_window.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window_event_handler.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#include <glm/gtc/constants.hpp>
#include <imgui/imgui.h>

#include <fmt/core.h>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string>

using erhe::geometry::get_pointf;
using erhe::geometry::to_glm_vec3;

namespace editor {

namespace {

constexpr float     c_brush_line_width = -2.0f; // pixels
constexpr glm::vec4 c_brush_color{1.0f, 0.85f, 0.2f, 1.0f};

constexpr const char* c_paint_channel_names[] = { "r", "g", "b", "a" };

} // anonymous namespace

auto c_str(const Paint_stroke_outcome outcome) -> const char*
{
    switch (outcome) {
        case Paint_stroke_outcome::none:      return "none";
        case Paint_stroke_outcome::ended:     return "ended";
        case Paint_stroke_outcome::cancelled: return "cancelled";
        case Paint_stroke_outcome::refused:   return "refused";
    }
    return "";
}

#pragma region Commands
Paint_vertex_command::Paint_vertex_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Paint_tool.paint_vertex"}
    , m_context{context}
{
}

void Paint_vertex_command::try_ready()
{
    if (!m_context.paint_tool->is_enabled()) {
        return;
    }

    if (m_context.paint_tool->try_ready()) {
        set_ready();
    }
}

auto Paint_vertex_command::try_call() -> bool
{
    if (!m_context.paint_tool->is_enabled()) {
        return false;
    }

    if (get_command_state() == erhe::commands::State::Ready) {
        set_active();
    }

    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }
    if (m_context.paint_tool->get_hover_scene_view() == nullptr) {
        set_inactive();
        return false;
    }

    m_context.paint_tool->paint();
    return true;
}

void Paint_vertex_command::on_inactive()
{
    // Stroke end: mouse release, hover loss, or XR trigger release. Releases
    // the optimization holds the stroke took (requirement 11).
    if (m_context.paint_tool != nullptr) {
        m_context.paint_tool->end_stroke();
    }
}

Paint_brush_radius_command::Paint_brush_radius_command(erhe::commands::Commands& commands, Paint_tool& tool)
    : Command{commands, "Paint_tool.brush_radius"}
    , m_tool {tool}
{
}

auto Paint_brush_radius_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    return m_tool.adjust_brush_radius(input.variant.vector2.relative_value.y);
}

Paint_brush_cancel_command::Paint_brush_cancel_command(erhe::commands::Commands& commands, Paint_tool& tool)
    : Command{commands, "Paint_tool.brush_cancel"}
    , m_tool {tool}
{
}

auto Paint_brush_cancel_command::try_call() -> bool
{
    return m_tool.cancel_brush_stroke();
}

#pragma endregion Commands

Paint_tool::Paint_tool(
    Paint_brush_config&          config,
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context,
    App_message_bus&             app_message_bus,
    Headset_view&                headset_view,
    Icon_set&                    icon_set,
    Tools&                       tools
)
    : Tool                          {context, tools, Tool_flags::toolbox}
    , m_config                      {config}
    , m_window                      {imgui_renderer, imgui_windows, "Paint Tool", "paint_tool", [this]() { window_imgui(); }}
    , m_paint_vertex_command        {commands, context}
    , m_drag_redirect_update_command{commands, m_paint_vertex_command}
    , m_drag_enable_command         {commands, m_drag_redirect_update_command}
    , m_brush_radius_command        {commands, *this}
    , m_brush_cancel_command        {commands, *this}
    , m_channel_stroke              {context}
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority  (c_priority);
    set_description    ("Paint Tool");
    set_icon           (icon_set.custom_icons, icon_set.icons.brush_small);

    m_paint_vertex_command        .set_host(this);
    m_drag_redirect_update_command.set_host(this);
    m_drag_enable_command         .set_host(this);
    m_brush_radius_command        .set_host(this);
    m_brush_cancel_command        .set_host(this);

    commands.register_command(&m_paint_vertex_command);
    commands.register_command(&m_drag_redirect_update_command);
    commands.register_command(&m_drag_enable_command);
    commands.register_command(&m_brush_radius_command);
    commands.register_command(&m_brush_cancel_command);
    commands.bind_command_to_mouse_drag(&m_paint_vertex_command, erhe::window::Mouse_button_left, true);
    // Brush mode only; declining leaves the plain wheel's zoom and other tools' Escape alone.
    commands.bind_command_to_mouse_wheel(&m_brush_radius_command, erhe::window::Key_modifier_bit_shift);
    commands.bind_command_to_key        (&m_brush_cancel_command, erhe::window::Key_escape, erhe::commands::Button_trigger::Button_pressed);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::Headset*    headset  = headset_view.get_headset();
    erhe::xr::Xr_actions* xr_right = (headset != nullptr) ? headset->get_actions_right() : nullptr;
    if (xr_right != nullptr) {
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right->a_click,       erhe::commands::Button_trigger::Any);
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right->trigger_click, erhe::commands::Button_trigger::Any);
        commands.bind_command_to_update           (&m_drag_redirect_update_command);
    }
#else
    static_cast<void>(headset_view);
#endif
    //// tools.register_tool(this);

    m_palette.emplace_back(240.0f / 255.0f, 163.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    m_palette.emplace_back(  0.0f / 255.0f, 117.0f / 255.0f, 220.0f / 255.0f, 1.0f);
    m_palette.emplace_back(153.0f / 255.0f,  63.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back( 76.0f / 255.0f,   0.0f / 255.0f,  92.0f / 255.0f, 1.0f);
    m_palette.emplace_back( 25.0f / 255.0f,  25.0f / 255.0f,  25.0f / 255.0f, 1.0f);
    m_palette.emplace_back(  0.0f / 255.0f,  92.0f / 255.0f,  49.0f / 255.0f, 1.0f);
    m_palette.emplace_back( 43.0f / 255.0f, 206.0f / 255.0f,  72.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f, 204.0f / 255.0f, 153.0f / 255.0f, 1.0f);
    m_palette.emplace_back(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_palette.emplace_back(148.0f / 255.0f, 255.0f / 255.0f, 181.0f / 255.0f, 1.0f);
    m_palette.emplace_back(143.0f / 255.0f, 124.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back(157.0f / 255.0f, 204.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back(194.0f / 255.0f,   0.0f / 255.0f, 136.0f / 255.0f, 1.0f);
    m_palette.emplace_back(  0.0f / 255.0f,  51.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f, 164.0f / 255.0f,   5.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f, 168.0f / 255.0f, 187.0f / 255.0f, 1.0f);
    m_palette.emplace_back( 66.0f / 255.0f, 102.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f,   0.0f / 255.0f,  16.0f / 255.0f, 1.0f);
    m_palette.emplace_back( 94.0f / 255.0f, 241.0f / 255.0f, 242.0f / 255.0f, 1.0f);
    m_palette.emplace_back(  0.0f / 255.0f, 153.0f / 255.0f, 143.0f / 255.0f, 1.0f);
    m_palette.emplace_back(224.0f / 255.0f, 255.0f / 255.0f, 102.0f / 255.0f, 1.0f);
    m_palette.emplace_back(116.0f / 255.0f,  10.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    m_palette.emplace_back(153.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f, 255.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f, 255.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f,  80.0f / 255.0f,   5.0f / 255.0f, 1.0f);

    m_ngon_colors.emplace_back(230.0f / 255.0f,  25.0f / 255.0f,  75.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back( 60.0f / 255.0f, 180.0f / 255.0f,  75.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 225.0f / 255.0f,  25.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f, 130.0f / 255.0f, 200.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(245.0f / 255.0f, 130.0f / 255.0f,  48.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(145.0f / 255.0f,  30.0f / 255.0f, 180.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back( 70.0f / 255.0f, 240.0f / 255.0f, 240.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(240.0f / 255.0f,  50.0f / 255.0f, 230.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(210.0f / 255.0f, 245.0f / 255.0f,  60.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(250.0f / 255.0f, 190.0f / 255.0f, 212.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(220.0f / 255.0f, 190.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(170.0f / 255.0f, 110.0f / 255.0f,  40.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 250.0f / 255.0f, 200.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(128.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(170.0f / 255.0f, 255.0f / 255.0f, 195.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(128.0f / 255.0f, 128.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 215.0f / 255.0f, 180.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f,   0.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, 1.0f);

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [&](Hover_scene_view_message& message) {
            Tool::on_message(message);
            update_brush();
        }
    );
    m_hover_mesh_subscription = app_message_bus.hover_mesh.subscribe(
        [this](Hover_mesh_message&) {
            on_hover_mesh();
        }
    );
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(static_cast<const erhe::Item_host*>(message.scene_root.get()));
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );

    m_config.radius   = std::clamp(m_config.radius,   c_paint_brush_min_radius, c_paint_brush_max_radius);
    m_config.strength = std::clamp(m_config.strength, 0.0f, 1.0f);
    m_config.target   = std::clamp(m_config.target,   0.0f, 1.0f);
}

void Paint_tool::tool_render(const Render_context& context)
{
    if (!is_enabled()) {
        return;
    }
    if (m_paint_mode == Paint_mode::Brush) {
        if (!m_brush_center.has_value() || !context.scene_root || (m_brush_scene.lock() != context.scene_root)) {
            return;
        }
        erhe::renderer::Primitive_renderer brush_renderer = context.get(
            {
                .primitive_type    = erhe::graphics::Primitive_type::line,
                .stencil_reference = 2,
                .draw_visible      = true,
                .draw_hidden       = true,
                .thin_lines        = false,
                .xray              = true
            }
        );
        for (std::size_t circle = 0; circle < 3; ++circle) {
            const std::size_t base = circle * c_paint_brush_circle_segments;
            for (std::size_t i = 0; i < c_paint_brush_circle_segments; ++i) {
                const glm::vec3& p0 = m_brush_circle_points[base + i];
                const glm::vec3& p1 = m_brush_circle_points[base + ((i + 1) % c_paint_brush_circle_segments)];
                brush_renderer.add_line(c_brush_color, c_brush_line_width, p0, c_brush_color, c_brush_line_width, p1);
            }
        }
        return;
    }
    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});
    line_renderer.set_thickness(5.0f);

    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    const Hover_entry& content = scene_view->get_hover(Hover_entry::content_slot);
    std::shared_ptr<erhe::scene::Mesh> scene_mesh = content.scene_mesh_weak.lock();
    if (
        !content.valid                ||
        !content.position.has_value() ||
        !content.normal.has_value()   ||
        !scene_mesh                   ||
        !content.geometry
    ) {
        return;
    }

    ERHE_VERIFY(content.scene_mesh_primitive_index != std::numeric_limits<std::size_t>::max());

    erhe::geometry::Geometry& geometry = *content.geometry.get();
    GEO::Mesh& geo_mesh = geometry.get_mesh();

    const GEO::index_t facet = content.facet;
    const GEO::index_t corner_count = geo_mesh.facets.nb_corners(content.facet);
    if (corner_count < 3) {
        return;
    }

    const glm::vec3 hover_position_in_world = content.position.value();

    const erhe::scene::Node* node = scene_mesh.get();
    if (node == nullptr) {
        return;
    }

    const glm::vec3 hover_position_in_mesh = node->transform_point_from_world_to_local(hover_position_in_world);

    float        max_distance_squared = std::numeric_limits<float>::max();
    GEO::index_t nearest_vertex       = 0;
    GEO::index_t nearest_corner       = 0;
    m_render_vertices.clear();
    for (GEO::index_t corner : geo_mesh.facets.corners(facet)) {
        const GEO::index_t vertex = geo_mesh.facet_corners.vertex(corner);
        const GEO::vec3f   p_in_mesh_ = get_pointf(geo_mesh.vertices, vertex);
        const glm::vec3    p_in_mesh = to_glm_vec3(p_in_mesh_);
        m_render_vertices.push_back(Render_vertex{.vertex = vertex, .position = p_in_mesh});
        const float d2 = glm::distance2(hover_position_in_mesh, p_in_mesh);
        if (d2 < max_distance_squared) {
            max_distance_squared = d2;
            nearest_vertex = vertex;
            nearest_corner = corner;
        }
    }

    glm::vec3 n = glm::normalize(content.normal.value());

    for (const Render_vertex& vertex_position : m_render_vertices) {
        const glm::vec3 p = node->transform_point_from_local_to_world(vertex_position.position);
        line_renderer.add_lines(
            (vertex_position.vertex == nearest_vertex) 
                ? glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}
                : glm::vec4{1.0f, 0.0f, 1.0f, 1.0f},
            {{ p, p + 0.2f * n }}
        );
    }
}

void Paint_tool::handle_priority_update(const int old_priority, const int new_priority)
{
    if (new_priority < old_priority) {
        // A drag in progress ends here: a Brush stroke is cancelled, the other modes' stroke ends as a release would.
        if (m_channel_stroke.is_active()) {
            finish_brush_stroke(Paint_stroke_outcome::cancelled);
        }
        m_paint_vertex_command.set_inactive();
        restore_debug_view();
        disable_command_host();
        m_brush_center.reset();
    }
    if (new_priority > old_priority) {
        enable_command_host();
        update_brush();
    }
}

auto Paint_tool::try_ready() -> bool
{
    if (!Command_host::is_enabled()) {
        return false;
    }

    auto* scene_view = get_hover_scene_view();

    if (scene_view == nullptr) {
        return false;
    }

    const Hover_entry* hover = scene_view->get_nearest_hover(Hover_entry::all_bits);
    return (hover != nullptr) && (hover->mask == Hover_entry::content_bit) && hover->valid;
}

auto Paint_tool::vertex_buffer_index_from_scnene_mesh_primitive_corner(
    const erhe::scene::Mesh& scene_mesh,
    const std::size_t        scene_mesh_primitive_index,
    const GEO::index_t       geo_mesh_corner
) -> std::optional<uint32_t>
{
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = scene_mesh.get_primitives();
    if (scene_mesh_primitive_index >= mesh_primitives.size()) {
        return {};
    }
    const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives.at(scene_mesh_primitive_index);
    const erhe::primitive::Primitive&  primitive      = *mesh_primitive.primitive.get();
    if (!primitive.render_shape) {
        return {};
    }
    const erhe::primitive::Element_mappings& element_mappings = primitive.render_shape->get_element_mappings();
    if (geo_mesh_corner >= element_mappings.mesh_corner_to_vertex_buffer_index.size()) {
        return {};
    }
    return element_mappings.mesh_corner_to_vertex_buffer_index[geo_mesh_corner];
}

void Paint_tool::end_stroke()
{
    if (m_channel_stroke.is_active()) {
        finish_brush_stroke(Paint_stroke_outcome::ended);
    }
    m_brush_press = Brush_press::idle;

    // Release the optimization holds first: the operations queued below swap
    // in freshly rebuilt primitives whose optimized variant must be
    // publishable.
    for (const std::shared_ptr<erhe::primitive::Primitive>& held : m_stroke_holds) {
        held->release_optimization_hold();
    }
    m_stroke_holds.clear();

    // One undoable Paint_colors_operation per touched geometry. The stroke
    // already wrote the final ("after") colors into the geometry and the fill
    // mesh; queueing executes the operation, whose primitive rebuild refreshes
    // the auxiliary streams and republishes the optimized variant (the
    // attribute rewrite is idempotent).
    std::vector<std::shared_ptr<Operation>> operations;
    for (Stroke_paint& stroke_paint : m_stroke_paints) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = stroke_paint.mesh.lock();
        if (!mesh || !stroke_paint.geometry || stroke_paint.corners.empty()) {
            continue;
        }
        const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
        if ((stroke_paint.primitive_index >= mesh_primitives.size()) || !mesh_primitives[stroke_paint.primitive_index].primitive) {
            continue;
        }
        const erhe::primitive::Primitive& primitive = *mesh_primitives[stroke_paint.primitive_index].primitive.get();
        if (!primitive.render_shape) {
            continue;
        }
        operations.push_back(
            std::make_shared<Paint_colors_operation>(
                Paint_colors_operation::Parameters{
                    .mesh            = mesh,
                    .primitive_index = stroke_paint.primitive_index,
                    .geometry        = stroke_paint.geometry,
                    .corners         = std::move(stroke_paint.corners),
                    .before_colors   = std::move(stroke_paint.before_colors),
                    .after_colors    = std::move(stroke_paint.after_colors),
                    .build_info      = erhe::primitive::Build_info{
                        .primitive_types = {
                            .fill_triangles          = true,
                            .fill_triangles_expanded = true,
                            .edge_lines              = true,
                            .corner_points           = true,
                            .centroid_points         = true
                        },
                        // Skinned meshes must rebuild into the skinned vertex
                        // format or the GPU streams silently lose their joints.
                        .buffer_info = mesh->skin
                            ? m_context.mesh_memory->make_skinned_primitive_buffer_info()
                            : m_context.mesh_memory->make_primitive_buffer_info()
                    },
                    .normal_style    = primitive.render_shape->get_normal_style()
                }
            )
        );
    }
    m_stroke_paints.clear();

    if (operations.empty()) {
        return;
    }
    if (operations.size() == 1) {
        m_context.operation_stack->queue(operations.front());
    } else {
        m_context.operation_stack->queue(
            std::make_shared<Compound_operation>(
                Compound_operation::Parameters{.operations = std::move(operations)}
            )
        );
    }
}

void Paint_tool::paint_corner(
    erhe::scene::Mesh& scene_mesh,
    const std::size_t  scene_mesh_primitive_index,
    const GEO::index_t corner,
    const glm::vec4    color
)
{
    const auto vertex_id_opt = vertex_buffer_index_from_scnene_mesh_primitive_corner(scene_mesh, scene_mesh_primitive_index, corner);
    if (!vertex_id_opt.has_value()) {
        return;
    }

    // Durable half of the stroke: write the color into the geometry's
    // corner_color_0 attribute - the attribute the primitive builder prefers -
    // and record the corner's before/after values so end_stroke() can queue
    // one undoable Paint_colors_operation per touched geometry. Geometry-less
    // primitives (triangle-soup builds) keep the GPU-only behavior.
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = scene_mesh.get_primitives();
    const erhe::scene::Mesh_primitive&              mesh_primitive  = mesh_primitives.at(scene_mesh_primitive_index);
    const std::shared_ptr<erhe::geometry::Geometry> geometry        =
        (mesh_primitive.primitive && mesh_primitive.primitive->render_shape)
            ? mesh_primitive.primitive->render_shape->get_geometry()
            : std::shared_ptr<erhe::geometry::Geometry>{};
    if (geometry) {
        Stroke_paint* stroke_paint = nullptr;
        for (Stroke_paint& candidate : m_stroke_paints) {
            if (candidate.geometry.get() == geometry.get()) {
                stroke_paint = &candidate;
                break;
            }
        }
        if (stroke_paint == nullptr) {
            m_stroke_paints.push_back(
                Stroke_paint{
                    .mesh            = std::static_pointer_cast<erhe::scene::Mesh>(scene_mesh.shared_from_this()),
                    .primitive_index = scene_mesh_primitive_index,
                    .geometry        = geometry
                }
            );
            stroke_paint = &m_stroke_paints.back();
        }

        erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
        const auto slot_it = stroke_paint->corner_to_slot.find(corner);
        if (slot_it == stroke_paint->corner_to_slot.end()) {
            // First touch of this corner in the stroke: capture what the
            // builder would have rendered as the undo value (corner color,
            // else vertex color, else the build's constant white).
            const GEO::index_t              vertex       = geometry->get_mesh().facet_corners.vertex(corner);
            const std::optional<GEO::vec4f> corner_color = attributes.corner_color_0.try_get(corner);
            const std::optional<GEO::vec4f> vertex_color = attributes.vertex_color_0.try_get(vertex);
            const GEO::vec4f                before       =
                corner_color.has_value() ? corner_color.value() :
                vertex_color.has_value() ? vertex_color.value() : GEO::vec4f{1.0f, 1.0f, 1.0f, 1.0f};
            stroke_paint->corner_to_slot.emplace(corner, stroke_paint->corners.size());
            stroke_paint->corners      .push_back(corner);
            stroke_paint->before_colors.push_back(glm::vec4{before.x, before.y, before.z, before.w});
            stroke_paint->after_colors .push_back(color);
        } else {
            stroke_paint->after_colors[slot_it->second] = color;
        }
        attributes.corner_color_0.set(corner, GEO::vec4f{color.x, color.y, color.z, color.w});
    }

    paint_vertex(scene_mesh, scene_mesh_primitive_index, vertex_id_opt.value(), color);
}

void Paint_tool::paint_vertex(
    erhe::scene::Mesh& scene_mesh,
    const std::size_t  scene_mesh_primitive_index,
    const uint32_t     vertex_id,
    const glm::vec4    color
)
{
    auto& mesh_memory = *m_context.mesh_memory;

    std::vector<std::uint8_t> buffer;

    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = scene_mesh.get_primitives();
    const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives.at(scene_mesh_primitive_index);
    const erhe::primitive::Primitive&  primitive      = *mesh_primitive.primitive.get();
    if (!primitive.render_shape) {
        return ;
    }
    // A deferred base variant (load.base_variant on_demand) has no vertex
    // buffer to paint into: the stroke requests its build and skips the
    // primitive until it has committed.
    if (primitive.render_shape->is_base_deferred()) {
        request_deferred_base_build(m_context, std::static_pointer_cast<erhe::scene::Mesh>(scene_mesh.shared_from_this()), Deferred_base_build::full);
        return;
    }

    // Requirement 11: the first touch of this primitive in the stroke drops
    // the optimized variant (the edit is per corner; the welded build cannot
    // express it) and takes an optimization hold, so no variant built from
    // pre-stroke data can be published until end_stroke() releases it. Lazy
    // per touched primitive: a hover-driven stroke can cross meshes, and a
    // mid-stroke primitive swap (undo) gets its own hold.
    const bool already_held = std::any_of(
        m_stroke_holds.begin(),
        m_stroke_holds.end(),
        [&mesh_primitive](const std::shared_ptr<erhe::primitive::Primitive>& held) {
            return held == mesh_primitive.primitive;
        }
    );
    if (!already_held) {
        std::shared_ptr<erhe::primitive::Primitive> held = scene_mesh.begin_optimized_variant_edit(scene_mesh_primitive_index);
        if (held) {
            m_stroke_holds.push_back(std::move(held));
        }
    }

    // GPU vertex edits always address the per-corner original buffer: the
    // element mappings only describe that variant, and an optimized variant is
    // invalidated by the edit rather than written through.
    const erhe::primitive::Buffer_mesh&             buffer_mesh      = primitive.render_shape->get_renderable_mesh();
    const erhe::scene_renderer::Vertex_input_entry& vertex_input     = mesh_memory.get_vertex_input(buffer_mesh.vertex_input_key);
    const erhe::dataformat::Vertex_format&          vertex_format    = vertex_input.vertex_format;
    const erhe::dataformat::Attribute_stream        attribute_stream = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::color, 0);
    if (attribute_stream.attribute == nullptr) {
        return;
    }

    const std::size_t stream_index      = attribute_stream.stream - vertex_format.streams.data();
    const std::size_t vertex_offset     = vertex_id * attribute_stream.stream->stride + attribute_stream.attribute->offset;
    const std::size_t range_byte_offset = buffer_mesh.vertex_buffer_ranges.at(stream_index).byte_offset;
    const erhe::primitive::Buffer_range stream_vertex_buffer_range = buffer_mesh.vertex_buffer_ranges.at(stream_index);
    erhe::primitive::Buffer_range vertex_buffer_update_range{
        .count        = 1,
        .element_size = erhe::dataformat::get_format_size_bytes(attribute_stream.attribute->format),
        .byte_offset  = range_byte_offset + vertex_offset,
        .pool_id      = stream_vertex_buffer_range.pool_id,
        .buffer_id    = stream_vertex_buffer_range.buffer_id
    };
    if (attribute_stream.attribute->format == erhe::dataformat::Format::format_32_vec4_float) {
        buffer.resize(sizeof(float) * 4);
        auto* const ptr = reinterpret_cast<float*>(buffer.data());
        ptr[0] = color.x;
        ptr[1] = color.y;
        ptr[2] = color.z;
        ptr[3] = color.w;
        mesh_memory.enqueue_vertex_data(
            vertex_buffer_update_range,
            std::move(buffer)
        );
    } else if (attribute_stream.attribute->format == erhe::dataformat::Format::format_8_vec4_unorm) {
        buffer.resize(sizeof(uint8_t) * 4);
        auto* const ptr = reinterpret_cast<uint8_t*>(buffer.data());
        ptr[0] = erhe::dataformat::float_to_unorm8(color.x);
        ptr[1] = erhe::dataformat::float_to_unorm8(color.y);
        ptr[2] = erhe::dataformat::float_to_unorm8(color.z);
        ptr[3] = erhe::dataformat::float_to_unorm8(color.w);
        mesh_memory.enqueue_vertex_data(
            vertex_buffer_update_range,
            std::move(buffer)
        );
    }
    // TODO To flush here, we'd need a command buffer.
    //      For now, we assume flush happens once per frame.
    // mesh_memory.flush();
}

void Paint_tool::paint()
{
    if (m_paint_mode == Paint_mode::Brush) {
        paint_brush();
        return;
    }
    m_vertex = GEO::NO_INDEX;
    m_corner = GEO::NO_INDEX;

    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    const Hover_entry& content = scene_view->get_hover(Hover_entry::content_slot);
    if (!content.valid || !content.position.has_value() || !content.geometry) {
        return;
    }

    std::shared_ptr<erhe::scene::Mesh> scene_mesh = content.scene_mesh_weak.lock();
    if (!scene_mesh) {
        return;
    }
    ERHE_VERIFY(content.scene_mesh_primitive_index != std::numeric_limits<std::size_t>::max());

    erhe::geometry::Geometry& geometry = *content.geometry.get();
    GEO::Mesh& geo_mesh = geometry.get_mesh();

    const GEO::index_t facet = content.facet;
    const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
    if (corner_count < 3) {
        return;
    }

    const glm::vec3 hover_position_in_world = content.position.value();

    const erhe::scene::Node* node = scene_mesh.get();
    if (node == nullptr) {
        return;
    }

    const glm::vec3 hover_position_in_mesh = node->transform_point_from_world_to_local(hover_position_in_world);

    float        max_distance_squared = std::numeric_limits<float>::max();
    GEO::index_t nearest_vertex = GEO::NO_INDEX;
    GEO::index_t nearest_corner = GEO::NO_INDEX;
    for (GEO::index_t corner : geo_mesh.facets.corners(facet)) {
        const GEO::index_t vertex = geo_mesh.facet_corners.vertex(corner);
        const GEO::vec3f p_ = get_pointf(geo_mesh.vertices, vertex);
        const glm::vec3 p = to_glm_vec3(p_);
        const float d2 = glm::distance2(hover_position_in_mesh, p);
        if (d2 < max_distance_squared) {
            max_distance_squared = d2;
            nearest_vertex = vertex;
            nearest_corner = corner;
        }
    }

    m_vertex = nearest_vertex;
    m_corner = nearest_corner;

    glm::vec4 color = m_palette.at(m_selected_palette_slot);
    switch (m_paint_mode) {
        case Paint_mode::Corner: {
            paint_corner(*scene_mesh, content.scene_mesh_primitive_index, nearest_corner, color);
            break;
        }
        case Paint_mode::Point: {
            const GEO::index_t vertex = geo_mesh.facet_corners.vertex(nearest_corner);
            const std::span<const GEO::index_t> vertex_corners = geometry.get_vertex_corners(vertex);
            for (GEO::index_t corner : vertex_corners) {
                paint_corner(*scene_mesh, content.scene_mesh_primitive_index, corner, color);
            }
            break;
        }
        case Paint_mode::Polygon: {
            for (GEO::index_t corner : geo_mesh.facets.corners(facet)) {
                paint_corner(*scene_mesh, content.scene_mesh_primitive_index, corner, color);
            }
            break;
        }
        case Paint_mode::Brush: {
            break;
        }
    }
}

void Paint_tool::tool_properties(erhe::imgui::Imgui_window&)
{
    int mode = static_cast<int>(m_paint_mode);
    if (ImGui::Combo("Mode", &mode, c_paint_mode_strings, IM_ARRAYSIZE(c_paint_mode_strings))) {
        set_paint_mode(static_cast<Paint_mode>(mode));
    }

    ImGui::ColorEdit4("Color", &m_palette.at(m_selected_palette_slot).x, ImGuiColorEditFlags_Float);

    int slot = 0;
    int row_length = 1;
    for (glm::vec4& entry : m_palette) {
        ImGui::PushID(slot);
        if (slot > 0) {
            if (row_length < 10) {
                ImGui::SameLine();
                row_length++;
            } else {
                row_length = 1;
            }
        }
        ImVec4 color{entry.x, entry.y, entry.z, entry.w};
        ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoTooltip;

        if (ImGui::ColorButton("##", entry, flags)) {
            m_selected_palette_slot = slot;
        }
        ImGui::PopID();
        ++slot;
    }

    if (m_paint_mode == Paint_mode::Brush) {
        brush_properties();
    }
}

void Paint_tool::window_imgui()
{
    const float ui_scale = m_context.app_settings->get_ui_scale();
    const ImVec2 button_size{110.0f * ui_scale, 0.0f};

    ImGui::SetNextItemWidth(200);
    int mode = static_cast<int>(m_paint_mode);
    if (ImGui::Combo("Paint mode", &mode, c_paint_mode_strings, IM_ARRAYSIZE(c_paint_mode_strings))) {
        set_paint_mode(static_cast<Paint_mode>(mode));
    }

    if (m_vertex != GEO::NO_INDEX) {
        ImGui::Text("Vertex: %u", m_vertex);
    }
    if (m_corner != GEO::NO_INDEX) {
        ImGui::Text("Corner: %u", m_corner);
    }

    {
        int corner_count = 3;
        for (auto& ngon_color : m_ngon_colors) {
            std::string label = fmt::format("{}-gon", corner_count);
            ImGui::ColorEdit4(label.c_str(), &ngon_color.x, ImGuiColorEditFlags_Float);
            ++corner_count;
        }
        std::string label = fmt::format("{}-gon", corner_count);
        if (ImGui::Button(label.c_str(), button_size)) {
            m_ngon_colors.push_back(glm::vec4{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    if (ImGui::Button("Color Selection") && !m_ngon_colors.empty()) {
        const auto& selection = m_context.selection->get_selected_items();
        for (const auto& item : selection) {
            auto* node = std::dynamic_pointer_cast<erhe::scene::Node>(item).get();
            auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(item);
            if (!mesh) {
                if (node) {
                    mesh = erhe::scene::get_mesh(node);
                }
                if (!mesh) {
                    continue;
                }
            }
            const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
            for (size_t primitive_index = 0, end = mesh_primitives.size(); primitive_index < end; ++primitive_index) {
                const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives.at(primitive_index);
                erhe::primitive::Primitive&  primitive      = *mesh_primitive.primitive.get();
                if (!primitive.render_shape) {
                    continue;
                }
                const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive.render_shape->get_geometry();
                if (!geometry) {
                    continue;
                }
                const GEO::Mesh& geo_mesh = geometry->get_mesh();
                for (GEO::index_t facet : geo_mesh.facets) {
                    const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
                    const std::size_t color_index = std::min(
                        static_cast<std::size_t>(corner_count),
                        m_ngon_colors.size() - 1
                    );

                    const glm::vec4 color = m_ngon_colors.at(color_index);
                    for (GEO::index_t corner : geo_mesh.facets.corners(facet)) {
                        paint_corner(*mesh, primitive_index, corner, color);
                    }
                }
            }
        }
    }
}

#pragma region Brush
void Paint_tool::touch_settings()
{
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Paint_tool::get_config() const -> const Paint_brush_config&
{
    return m_config;
}

auto Paint_tool::get_paint_mode() const -> Paint_mode
{
    return m_paint_mode;
}

void Paint_tool::set_paint_mode(const Paint_mode mode)
{
    if ((mode != Paint_mode::Brush) && m_channel_stroke.is_active()) {
        finish_brush_stroke(Paint_stroke_outcome::cancelled);
        m_brush_press = Brush_press::stopped;
    }
    m_paint_mode = mode;
    update_brush();
}

void Paint_tool::set_brush_target(const Paint_brush_target target)
{
    m_config.target_kind = target;
    touch_settings();
}

void Paint_tool::set_brush_radius(const float radius)
{
    m_config.radius = std::clamp(radius, c_paint_brush_min_radius, c_paint_brush_max_radius);
    // The open stroke's next dabs take the new radius.
    if (m_channel_stroke.is_active()) {
        m_channel_stroke.get_settings().radius = m_config.radius;
    }
    update_brush_circles();
    touch_settings();
}

void Paint_tool::set_brush_strength(const float strength)
{
    m_config.strength = std::clamp(strength, 0.0f, 1.0f);
    touch_settings();
}

void Paint_tool::set_brush_value(const float value)
{
    m_config.target = std::clamp(value, 0.0f, 1.0f);
    touch_settings();
}

void Paint_tool::set_brush_blend(const Paint_brush_blend blend)
{
    m_config.blend = blend;
    touch_settings();
}

void Paint_tool::set_brush_channel(const Paint_brush_channel channel)
{
    m_config.channel = channel;
    touch_settings();
}

auto Paint_tool::is_brush_stroke_active() const -> bool
{
    return m_channel_stroke.is_active();
}

auto Paint_tool::get_brush_changed_vertex_count() const -> std::size_t
{
    return m_channel_stroke.get_changed_vertex_count();
}

auto Paint_tool::get_brush_center() const -> const std::optional<glm::vec3>&
{
    return m_brush_center;
}

auto Paint_tool::get_last_stroke() const -> const Paint_last_stroke&
{
    return m_last_stroke;
}

auto Paint_tool::get_last_hovered_mesh(std::size_t& out_primitive_index) const -> std::shared_ptr<erhe::scene::Mesh>
{
    out_primitive_index = m_last_hovered_primitive_index;
    return m_last_hovered_mesh.lock();
}

auto Paint_tool::get_weight_channel(const erhe::scene::Mesh* const mesh, const std::size_t primitive_index) const -> Paint_channel_choice
{
    const unsigned int tool_channel = static_cast<unsigned int>(m_config.channel);
    if (mesh == nullptr) {
        return Paint_channel_choice{.channel = tool_channel, .source = Paint_channel_source::no_mesh};
    }
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
    if (primitive_index < mesh_primitives.size()) {
        const std::shared_ptr<erhe::primitive::Material>& material = mesh_primitives[primitive_index].material;
        if (material && material->get_detail_blend()) {
            const erhe::primitive::Texture_channel channel = material->get_detail_blend_weight_channel();
            if (channel != erhe::primitive::Texture_channel::none) {
                return Paint_channel_choice{.channel = static_cast<unsigned int>(channel), .source = Paint_channel_source::material, .material = material};
            }
        }
        return Paint_channel_choice{.channel = tool_channel, .source = Paint_channel_source::tool, .material = material};
    }
    return Paint_channel_choice{.channel = tool_channel, .source = Paint_channel_source::tool};
}

void Paint_tool::on_hover_mesh()
{
    update_brush();
    dab_at_hover();
}

void Paint_tool::update_brush()
{
    if (!is_enabled() || (m_paint_mode != Paint_mode::Brush)) {
        m_brush_center.reset();
        return;
    }
    Scene_view* const scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        m_brush_center.reset();
        return;
    }
    const Hover_entry&                       content = scene_view->get_hover(Hover_entry::content_slot);
    const std::shared_ptr<erhe::scene::Mesh> mesh    = content.scene_mesh_weak.lock();
    if (!content.valid || !content.position.has_value() || !mesh) {
        m_brush_center.reset();
        return;
    }
    m_brush_center                 = content.position.value();
    m_brush_scene                  = scene_view->get_scene_root();
    m_last_hovered_mesh            = mesh;
    m_last_hovered_primitive_index = content.scene_mesh_primitive_index;
    update_brush_circles();
}

void Paint_tool::update_brush_circles()
{
    if (!m_brush_center.has_value()) {
        return;
    }
    // Three great circles of the brush sphere, in the world axis planes.
    const glm::vec3 axes[3][2] = {
        {glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}},
        {glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 1.0f}},
        {glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{1.0f, 0.0f, 0.0f}}
    };
    const glm::vec3 center = m_brush_center.value();
    const float     radius = m_config.radius;
    for (std::size_t circle = 0; circle < 3; ++circle) {
        for (std::size_t i = 0; i < c_paint_brush_circle_segments; ++i) {
            const float angle = (glm::two_pi<float>() * static_cast<float>(i)) / static_cast<float>(c_paint_brush_circle_segments);
            m_brush_circle_points[(circle * c_paint_brush_circle_segments) + i] =
                center + (radius * ((std::cos(angle) * axes[circle][0]) + (std::sin(angle) * axes[circle][1])));
        }
    }
}

void Paint_tool::paint_brush()
{
    if (m_brush_press == Brush_press::stopped) {
        return;
    }
    if (m_brush_press == Brush_press::idle) {
        Scene_view* const scene_view = get_hover_scene_view();
        if (scene_view == nullptr) {
            return;
        }
        const Hover_entry&                       content = scene_view->get_hover(Hover_entry::content_slot);
        const std::shared_ptr<erhe::scene::Mesh> mesh    = content.scene_mesh_weak.lock();
        if (!content.valid || !content.position.has_value() || !mesh) {
            return;
        }
        const std::size_t primitive_index = content.scene_mesh_primitive_index;
        Channel_brush_settings settings{
            .radius   = m_config.radius,
            .strength = m_config.strength,
            .blend    = static_cast<Channel_brush_blend>(static_cast<unsigned int>(m_config.blend))
        };
        if (m_config.target_kind == Paint_brush_target::color) {
            settings.target       = m_palette.at(m_selected_palette_slot);
            settings.channel_mask = c_channel_mask_all;
        } else {
            settings.target       = glm::vec4{m_config.target};
            settings.channel_mask = 1u << get_weight_channel(mesh.get(), primitive_index).channel;
        }
        const std::optional<std::string> refusal = m_channel_stroke.begin(mesh, primitive_index, settings);
        if (refusal.has_value()) {
            m_last_stroke = Paint_last_stroke{
                .outcome         = Paint_stroke_outcome::refused,
                .mesh_name       = mesh->get_name(),
                .primitive_index = primitive_index,
                .channel_mask    = settings.channel_mask,
                .refusal         = refusal.value()
            };
            m_brush_press = Brush_press::stopped;
            log_tools->info("Paint brush: no stroke: {}", refusal.value());
            return;
        }
        m_brush_press = Brush_press::painting;
    }
    dab_at_hover();
}

void Paint_tool::dab_at_hover()
{
    if (!m_channel_stroke.is_active()) {
        return;
    }
    Scene_view* const scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }
    // A stroke stays on the primitive it started on: elsewhere the pointer paints nothing.
    const Hover_entry& content = scene_view->get_hover(Hover_entry::content_slot);
    if (
        !content.valid || !content.position.has_value() ||
        (content.scene_mesh_primitive_index != m_channel_stroke.get_primitive_index()) ||
        (content.scene_mesh_weak.lock() != m_channel_stroke.get_mesh())
    ) {
        return;
    }
    m_channel_stroke.dab(content.position.value());
}

void Paint_tool::finish_brush_stroke(const Paint_stroke_outcome outcome)
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = m_channel_stroke.get_mesh();
    Paint_last_stroke last{
        .outcome          = outcome,
        .mesh_name        = mesh ? mesh->get_name() : std::string{},
        .primitive_index  = m_channel_stroke.get_primitive_index(),
        .channel_mask     = m_channel_stroke.get_settings().channel_mask,
        .changed_vertices = m_channel_stroke.get_changed_vertex_count()
    };
    if (outcome == Paint_stroke_outcome::ended) {
        const std::shared_ptr<Operation> operation = m_channel_stroke.end();
        if (operation) {
            m_context.operation_stack->queue(operation);
            last.recorded = true;
        }
    } else {
        m_channel_stroke.cancel();
    }
    log_tools->info(
        "Paint brush: stroke {} on '{}' primitive {}: {} vertices{}",
        c_str(outcome), last.mesh_name, last.primitive_index, last.changed_vertices, last.recorded ? ", one undo entry" : ""
    );
    m_last_stroke = std::move(last);
}

auto Paint_tool::cancel_brush_stroke() -> bool
{
    if (!m_channel_stroke.is_active()) {
        return false;
    }
    finish_brush_stroke(Paint_stroke_outcome::cancelled);
    // The rest of the drag paints nothing; the release ends it.
    m_brush_press = Brush_press::stopped;
    return true;
}

auto Paint_tool::adjust_brush_radius(const float wheel_steps) -> bool
{
    if (!is_enabled() || (m_paint_mode != Paint_mode::Brush) || (get_hover_scene_view() == nullptr) || (wheel_steps == 0.0f) || !std::isfinite(wheel_steps)) {
        return false;
    }
    set_brush_radius(m_config.radius * std::pow(c_paint_brush_radius_wheel_step, wheel_steps));
    return true;
}

void Paint_tool::on_close_scene(const erhe::Item_host* const closing_host)
{
    const std::shared_ptr<erhe::scene::Mesh>& stroke_mesh = m_channel_stroke.get_mesh();
    if (stroke_mesh && (stroke_mesh->get_item_host() == closing_host)) {
        finish_brush_stroke(Paint_stroke_outcome::cancelled);
        m_brush_press = Brush_press::stopped;
    }
    const std::shared_ptr<erhe::scene::Mesh> hovered = m_last_hovered_mesh.lock();
    if (!hovered || (hovered->get_item_host() == closing_host)) {
        m_last_hovered_mesh.reset();
    }
    const std::shared_ptr<Scene_root> brush_scene = m_brush_scene.lock();
    if (!brush_scene || (static_cast<const erhe::Item_host*>(brush_scene.get()) == closing_host)) {
        m_brush_scene.reset();
        m_brush_center.reset();
    }
}

void Paint_tool::on_items_removed(const Removed_items& removed)
{
    const std::shared_ptr<erhe::scene::Mesh>& stroke_mesh = m_channel_stroke.get_mesh();
    if (stroke_mesh && removed.lookup.contains(stroke_mesh.get())) {
        finish_brush_stroke(Paint_stroke_outcome::cancelled);
        m_brush_press = Brush_press::stopped;
    }
    const std::shared_ptr<erhe::scene::Mesh> hovered = m_last_hovered_mesh.lock();
    if (hovered && removed.lookup.contains(hovered.get())) {
        m_last_hovered_mesh.reset();
    }
}

auto Paint_tool::get_debug_view() const -> std::shared_ptr<Viewport_scene_view>
{
    std::shared_ptr<Viewport_scene_view> view = m_debug_view.lock();
    if (view && (view->get_shader_debug() == erhe::scene_renderer::Shader_debug::detail_blend_weight)) {
        return view;
    }
    return {};
}

void Paint_tool::restore_debug_view()
{
    // Only a view that still shows what the switch set gets its mode back: a later change by other means is kept.
    const std::shared_ptr<Viewport_scene_view> view = get_debug_view();
    if (view) {
        view->set_shader_debug(m_debug_view_previous);
    }
    m_debug_view.reset();
}

void Paint_tool::set_debug_view(const Paint_debug_view state)
{
    m_debug_view_message.clear();
    restore_debug_view();
    if (state == Paint_debug_view::off) {
        return;
    }
    const Scene_view* const              hovered = get_last_hover_scene_view();
    std::shared_ptr<Viewport_scene_view> view;
    if ((hovered != nullptr) && (m_context.scene_views != nullptr)) {
        for (const std::shared_ptr<Viewport_window>& viewport_window : m_context.scene_views->get_viewport_windows()) {
            std::shared_ptr<Viewport_scene_view> candidate = viewport_window->viewport_scene_view();
            if (candidate.get() == hovered) {
                view = std::move(candidate);
                break;
            }
        }
    }
    if (!view) {
        m_debug_view_message = "hover a viewport first";
        return;
    }
    const erhe::scene_renderer::Shader_debug current = view->get_shader_debug();
    m_debug_view_previous =
        (current == erhe::scene_renderer::Shader_debug::detail_blend_weight)
            ? erhe::scene_renderer::Shader_debug::none
            : current;
    view->set_shader_debug(erhe::scene_renderer::Shader_debug::detail_blend_weight);
    m_debug_view = view;
}

void Paint_tool::fill_selection(const Channel_fill_mode mode)
{
    // Grouped by the channel the Weight rule gives each mesh (its first primitive's material).
    std::array<std::vector<std::shared_ptr<erhe::scene::Mesh>>, 4> meshes_by_channel;
    std::size_t mesh_count = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
        std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(item);
        if (!mesh) {
            const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node) {
                mesh = erhe::scene::get_mesh(node.get());
            }
        }
        if (!mesh) {
            continue;
        }
        meshes_by_channel[get_weight_channel(mesh.get(), 0).channel & 3u].push_back(mesh);
        ++mesh_count;
    }
    if (mesh_count == 0) {
        m_fill_message = "select the meshes to fill first";
        return;
    }
    std::vector<std::shared_ptr<Operation>> operations;
    std::string error;
    for (unsigned int channel = 0; channel < 4; ++channel) {
        if (meshes_by_channel[channel].empty()) {
            continue;
        }
        std::shared_ptr<Operation> operation = make_channel_fill_operation(m_context, meshes_by_channel[channel], 1u << channel, mode, m_config.target, error);
        if (operation) {
            operations.push_back(std::move(operation));
        }
    }
    if (operations.empty()) {
        m_fill_message = error;
        return;
    }
    if (operations.size() == 1) {
        m_context.operation_stack->queue(operations.front());
    } else {
        m_context.operation_stack->queue(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
    }
    m_fill_message = fmt::format("{} {} mesh(es)", (mode == Channel_fill_mode::fill) ? "Filled" : "Inverted", mesh_count);
}

void Paint_tool::brush_properties()
{
    ImGui::SeparatorText("Brush");
    int target_kind = static_cast<int>(m_config.target_kind);
    if (ImGui::RadioButton("Weight", &target_kind, static_cast<int>(Paint_brush_target::weight))) {
        set_brush_target(Paint_brush_target::weight);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Color", &target_kind, static_cast<int>(Paint_brush_target::color))) {
        set_brush_target(Paint_brush_target::color);
    }
    float radius = m_config.radius;
    if (ImGui::SliderFloat("Brush Radius", &radius, c_paint_brush_min_radius, c_paint_brush_max_radius, "%.3f m", ImGuiSliderFlags_Logarithmic)) {
        set_brush_radius(radius);
    }
    float strength = m_config.strength;
    if (ImGui::SliderFloat("Strength", &strength, 0.0f, 1.0f, "%.2f")) {
        set_brush_strength(strength);
    }
    int blend = static_cast<int>(m_config.blend);
    if (ImGui::Combo("Blend", &blend, c_channel_brush_blend_strings, IM_ARRAYSIZE(c_channel_brush_blend_strings))) {
        set_brush_blend(static_cast<Paint_brush_blend>(blend));
    }

    std::size_t                              hovered_index = 0;
    const std::shared_ptr<erhe::scene::Mesh> hovered       = get_last_hovered_mesh(hovered_index);
    if (m_config.target_kind == Paint_brush_target::weight) {
        float value = m_config.target;
        if (ImGui::SliderFloat("Target", &value, 0.0f, 1.0f, "%.3f")) {
            set_brush_value(value);
        }
        int channel = static_cast<int>(m_config.channel);
        if (ImGui::Combo("Channel", &channel, c_paint_channel_names, IM_ARRAYSIZE(c_paint_channel_names))) {
            set_brush_channel(static_cast<Paint_brush_channel>(channel));
        }
        const Paint_channel_choice choice = get_weight_channel(hovered.get(), hovered_index);
        const char* const          name   = c_paint_channel_names[choice.channel & 3u];
        switch (choice.source) {
            case Paint_channel_source::material: {
                ImGui::TextWrapped("Paints %s: the weight channel of material '%s' (detail blend on)", name, choice.material->get_name().c_str());
                break;
            }
            case Paint_channel_source::tool: {
                ImGui::TextWrapped("Paints %s: the Channel value (the hovered material has detail blend off)", name);
                break;
            }
            case Paint_channel_source::no_mesh: {
                ImGui::TextWrapped("Paints %s: the Channel value (no mesh hovered yet)", name);
                break;
            }
        }
    } else {
        ImGui::TextUnformatted("Paints r, g, b, a toward the selected palette color");
    }

    if (m_last_stroke.outcome == Paint_stroke_outcome::refused) {
        ImGui::TextWrapped("Cannot paint: %s", m_last_stroke.refusal.c_str());
    } else if (m_last_stroke.outcome != Paint_stroke_outcome::none) {
        ImGui::Text("Last stroke %s: %zu vertices", c_str(m_last_stroke.outcome), m_last_stroke.changed_vertices);
    }

    if (ImGui::Button("Fill")) {
        fill_selection(Channel_fill_mode::fill);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", "Set the Weight channel of every vertex of the selected meshes to Target.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Invert")) {
        fill_selection(Channel_fill_mode::invert);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", "Set the Weight channel of every vertex of the selected meshes to 1 - its value.");
    }
    if (!m_fill_message.empty()) {
        ImGui::TextUnformatted(m_fill_message.c_str());
    }

    bool debug_view = static_cast<bool>(get_debug_view());
    if (ImGui::Checkbox("Show Detail Weight", &debug_view)) {
        set_debug_view(debug_view ? Paint_debug_view::on : Paint_debug_view::off);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", "Show the Detail Blend Weight debug mode in the viewport last hovered, and back.");
    }
    if (!m_debug_view_message.empty()) {
        ImGui::TextUnformatted(m_debug_view_message.c_str());
    }
}
#pragma endregion Brush

}
