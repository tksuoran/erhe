
#include "brushes/brushes.hpp"

#include "brushes/brush.hpp"
#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/material_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/grid_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "windows/materials_window.hpp"
#include "windows/operations.hpp"

#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/view.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/operation/clone.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/gtx/transform.hpp>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::mat4;
using glm::vec3;
using glm::vec4;
using erhe::geometry::Polygon; // Resolve conflict with wingdi.h BOOL Polygon(HDC,const POINT *,int)

auto Brush_tool_preview_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    if (
        (get_command_state() != erhe::application::State::Active) ||
        !m_brushes.is_enabled()
    )
    {
        return false;
    }
    m_brushes.on_motion();
    return true;
}

void Brush_tool_insert_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (m_brushes.try_insert_ready())
    {
        set_ready(context);
    }
}

auto Brush_tool_insert_command::try_call(
    erhe::application::Command_context& command_context
) -> bool
{
    if (get_command_state() != erhe::application::State::Ready)
    {
        return false;
    }
    const bool consumed = m_brushes.try_insert();
    set_inactive(command_context);
    return consumed;
}

Brushes::Brushes()
    : erhe::components::Component{c_type_name}
    , m_preview_command          {*this}
    , m_insert_command           {*this}
{
}

Brushes::~Brushes() noexcept
{
}

void Brushes::declare_required_components()
{
    require<erhe::application::Commands>();
    m_configuration = require<erhe::application::Configuration>();
    require<Editor_scenes>();
    require<Operations   >();
    require<Tools        >();
}

void Brushes::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    get<Tools>()->register_tool(this);

    const auto commands = get<erhe::application::Commands>();
    commands->register_command(&m_preview_command);
    commands->register_command(&m_insert_command);
    commands->bind_command_to_update     (&m_preview_command);
    commands->bind_command_to_mouse_click(&m_insert_command, erhe::toolkit::Mouse_button_right);

    get<Operations>()->register_active_tool(this);

    get<Editor_scenes>()->get_editor_message_bus()->add_receiver(
        [&](Editor_message& message)
        {
            on_message(message);
        }
    );
}

void Brushes::post_initialize()
{
    m_editor_scenes     = get<Editor_scenes   >();
    m_grid_tool         = get<Grid_tool       >();
    m_materials_window  = get<Materials_window>();
    m_operation_stack   = get<Operation_stack >();
    m_selection_tool    = get<Selection_tool  >();
    m_viewport_windows  = get<Viewport_windows>();
}

void Brushes::on_message(Editor_message& message)
{
    switch (message.event_type)
    {
        case Editor_event_type::selection_changed:
        {
            break;
        }

        case Editor_event_type::viewport_changed:
        {
            if (!message.new_viewport_window)
            {
                remove_brush_mesh();
            }
            break;
        }

        default:
        {
            break;
        }
    }
}

auto Brushes::make_brush(const Brush_data& create_info) -> std::shared_ptr<Brush>
{
    if (create_info.geometry)
    {
        create_info.geometry->build_edges();
        create_info.geometry->compute_polygon_normals();
        create_info.geometry->compute_tangents();
        create_info.geometry->compute_polygon_centroids();
        create_info.geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);
    }

    const auto brush = std::make_shared<Brush>(create_info);
    register_brush(brush);
    return brush;
}

void Brushes::register_brush(const std::shared_ptr<Brush>& brush)
{
    const std::lock_guard<std::mutex> lock{m_brush_mutex};
    m_brushes.push_back(brush);
}

void Brushes::remove_brush_mesh()
{
    if (m_brush_mesh)
    {
        ERHE_VERIFY(m_brush_mesh->node_data.host != nullptr);

        log_brush->trace("removing brush mesh");

        // TODO node destructor already does this, right?
        m_brush_mesh->set_parent({});

        m_brush_mesh.reset();
    }
}

auto Brushes::try_insert_ready() -> bool
{
    return is_enabled() && m_hover_content;
}

auto Brushes::try_insert() -> bool
{
    if (
        !m_brush_mesh ||
        !m_hover_position.has_value() ||
        (m_brush.expired())
    )
    {
        return false;
    }

        get<Editor_scenes>()->sanity_check();

    do_insert_operation();

        get<Editor_scenes>()->sanity_check();

    remove_brush_mesh();

        get<Editor_scenes>()->sanity_check();

    return true;
}

void Brushes::on_enable_state_changed()
{
    const auto& commands = get<erhe::application::Commands>();
    erhe::application::Command_context command_context{
        *commands.get()
    };

    if (is_enabled())
    {
        m_preview_command.set_active(command_context);
        on_motion();
    }
    else
    {
        m_preview_command.set_inactive(command_context);
        m_hover_content     = false;
        m_hover_tool        = false;
        m_hover_mesh    .reset();
        m_hover_primitive   = 0;
        m_hover_local_index = 0;
        m_hover_geometry    = nullptr;
        m_hover_position.reset();
        m_hover_normal  .reset();
        remove_brush_mesh();
    }
}

void Brushes::on_motion()
{
    const auto viewport_window = m_viewport_windows->hover_window();
    if (!viewport_window)
    {
        return;
    }

    const auto& content = viewport_window->get_hover(Hover_entry::content_slot);
    const auto& tool    = viewport_window->get_hover(Hover_entry::tool_slot);
    m_hover_content     = content.valid;
    m_hover_tool        = tool.valid;
    m_hover_mesh        = content.mesh;
    m_hover_primitive   = content.primitive;
    m_hover_local_index = content.local_index;
    m_hover_geometry    = content.geometry;
    m_hover_position    = content.position;
    m_hover_normal      = content.normal;

    if (
        (m_hover_geometry != nullptr) &&
        (m_hover_local_index > m_hover_geometry->get_polygon_count())
    )
    {
        m_hover_local_index = 0;
        m_hover_geometry    = nullptr;
    }

    if (m_hover_mesh && m_hover_position.has_value())
    {
        m_hover_position = m_hover_mesh->transform_direction_from_world_to_local(m_hover_position.value());
    }

    update_mesh();
}

// Returns transform which places brush in parent (hover) mesh space.
auto Brushes::get_brush_transform() -> mat4
{
    const auto brush = m_brush.lock();

    if (
        (m_hover_mesh     == nullptr) ||
        (m_hover_geometry == nullptr) ||
        !brush
    )
    {
        return mat4{1};
    }

    const auto polygon_id = static_cast<erhe::geometry::Polygon_id>(m_hover_local_index);
    if (m_hover_local_index >= m_hover_geometry->get_polygon_count())
    {
        return mat4{1};
    }

    const Polygon&  polygon    = m_hover_geometry->polygons[polygon_id];
    Reference_frame hover_frame(*m_hover_geometry, polygon_id, 0, 0);
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

    m_transform_scale = scale;
    if (scale != 1.0f)
    {
        const mat4 scale_transform = erhe::toolkit::create_scale(scale);
        brush_frame.transform_by(scale_transform);
    }

    debug_info.hover_frame_scale = hover_frame.scale();
    debug_info.brush_frame_scale = brush_frame.scale();
    debug_info.transform_scale   = scale;

    if (
        !m_snap_to_hover_polygon &&
        m_hover_position.has_value()
    )
    {
        hover_frame.centroid = m_hover_position.value();
        if (m_snap_to_grid)
        {
            hover_frame.centroid = m_grid_tool->snap(hover_frame.centroid);
        }
    }

    const mat4 hover_transform = hover_frame.transform();
    const mat4 brush_transform = brush_frame.transform();
    const mat4 inverse_brush   = inverse(brush_transform);
    const mat4 align           = hover_transform * inverse_brush;

    return align;
}

void Brushes::update_mesh_node_transform()
{
    const auto brush = m_brush.lock();
    if (
        !m_hover_position.has_value() ||
        !m_brush_mesh ||
        !m_hover_mesh ||
        !brush
    )
    {
        return;
    }

    const auto  transform    = get_brush_transform();
    const auto& brush_scaled = brush->get_scaled(m_transform_scale);
    const auto& brush_parent = m_brush_mesh->parent().lock();
    if (brush_parent != m_hover_mesh)
    {
        if (brush_parent)
        {
            log_brush->trace(
                "m_brush_mesh->parent() = {} ({})",
                brush_parent->name(),
                brush_parent->node_type()
            );
        }
        else
        {
            log_brush->trace("m_brush_mesh->parent() = (none)");
        }

        if (m_hover_mesh)
        {
            log_brush->trace(
                "m_hover_mesh = {} ({})",
                m_hover_mesh->name(),
                m_hover_mesh->node_type()
            );
        }
        else
        {
            log_brush->trace("m_hover_mesh = (none)");
        }

        if (m_hover_mesh)
        {
            m_brush_mesh->set_parent(m_hover_mesh);
        }
    }
    m_brush_mesh->set_parent_from_node(transform);

    auto& primitive = m_brush_mesh->mesh_data.primitives.front();
    primitive.gl_primitive_geometry = brush_scaled.gl_primitive_geometry;
    primitive.rt_primitive_geometry = brush_scaled.rt_primitive->primitive_geometry;
    primitive.rt_vertex_buffer      = brush_scaled.rt_primitive->vertex_buffer;
    primitive.rt_index_buffer       = brush_scaled.rt_primitive->index_buffer;
}

void Brushes::do_insert_operation()
{
    const auto brush = m_brush.lock();

    if (
        !m_hover_position.has_value() ||
        !brush
    )
    {
        return;
    }

    log_brush->trace(
        "{} scale = {}",
        __func__,
        m_transform_scale
    );

    const auto hover_from_brush = get_brush_transform();
    const uint64_t visibility_flags =
        erhe::scene::Node_visibility::visible     |
        erhe::scene::Node_visibility::content     |
        erhe::scene::Node_visibility::shadow_cast |
        erhe::scene::Node_visibility::id;

    ERHE_VERIFY(m_hover_mesh->node_data.host != nullptr);

    const Instance_create_info brush_instance_create_info
    {
        .node_visibility_flags = visibility_flags,
        .scene_root            = reinterpret_cast<Scene_root*>(m_hover_mesh->get_scene_host()),
        .world_from_node       = m_hover_mesh->world_from_node() * hover_from_brush,
        .material              = m_materials_window->selected_material(),
        .scale                 = m_transform_scale,
        .physics_enabled       = m_configuration->physics.static_enable
    };
    const auto instance = brush->make_instance(brush_instance_create_info);

    std::shared_ptr<erhe::scene::Node> parent = m_hover_mesh;
    const auto& selection = m_selection_tool->selection();
    if (!selection.empty())
    {
        parent = selection.front();
    }

    auto op = std::make_shared<Node_insert_remove_operation>(
        Node_insert_remove_operation::Parameters{
            .selection_tool = m_selection_tool.get(),
            .node           = instance.mesh,
            .parent         = parent,
            .mode           = Scene_item_operation::Mode::insert
        }
    );
    m_operation_stack->push(op);
}

void Brushes::add_brush_mesh()
{
    const auto brush = m_brush.lock();

    if (
        !brush ||
        !m_hover_position.has_value() ||
        !m_hover_mesh
    )
    {
        return;
    }

    auto material = m_materials_window->selected_material();
    if (!material)
    {
        log_brush->warn("No material selected");
        return;
    }

    auto* scene_root = reinterpret_cast<Scene_root*>(m_hover_mesh->node_data.host);
    ERHE_VERIFY(scene_root != nullptr);

    brush->late_initialize();
    const auto& brush_scaled = brush->get_scaled(m_transform_scale);
    m_brush_mesh = std::make_shared<erhe::scene::Mesh>(
        fmt::format("brush-{}", brush->name()),
        erhe::primitive::Primitive{
            .material              = material,
            .gl_primitive_geometry = brush_scaled.gl_primitive_geometry,
            .rt_primitive_geometry = brush_scaled.rt_primitive->primitive_geometry,
            .rt_vertex_buffer      = brush_scaled.rt_primitive->vertex_buffer,
            .rt_index_buffer       = brush_scaled.rt_primitive->index_buffer,
            .source_geometry       = brush_scaled.geometry,
            .normal_style          = brush->data.normal_style
        }
    );
    m_brush_mesh->set_visibility_mask(
        erhe::scene::Node_visibility::visible |
        erhe::scene::Node_visibility::content |
        erhe::scene::Node_visibility::brush
    );
    m_brush_mesh->node_data.flag_bits =
        m_brush_mesh->node_data.flag_bits |
        erhe::scene::Node_flag_bit::no_message;

    m_brush_mesh->mesh_data.layer_id = scene_root->layers().brush()->id.get_id();

    m_brush_mesh->set_parent(scene_root->scene().root_node);

    update_mesh_node_transform();
}

void Brushes::update_mesh()
{
    const auto brush = m_brush.lock();

    if (!m_brush_mesh)
    {
        if (
            !brush ||
            !m_hover_position.has_value() ||
            !m_hover_mesh
        )
        {
            return;
        }

        add_brush_mesh();
        return;
    }

    if (
        !brush ||
        !m_hover_position.has_value()
    )
    {
        remove_brush_mesh();
    }

    update_mesh_node_transform();
}

void Brushes::tool_properties()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    ImGui::Checkbox   ("Physics",         &m_with_physics);
    ImGui::DragInt    ("Face Offset",     &m_polygon_offset, 1.0f, 0, 100);
    ImGui::DragInt    ("Corner Offset",   &m_corner_offset,  1.0f, 0, 100);
    ImGui::InputFloat ("Hover scale",     &debug_info.hover_frame_scale);
    ImGui::InputFloat ("Brush scale",     &debug_info.brush_frame_scale);
    ImGui::InputFloat ("Transform scale", &debug_info.transform_scale);
    ImGui::SliderFloat("Scale",           &m_scale, 0.0f, 2.0f);
    erhe::application::make_check_box("Snap to Polygon", &m_snap_to_hover_polygon);
    erhe::application::make_check_box(
        "Snap to Grid",
        &m_snap_to_grid,
        m_snap_to_hover_polygon
            ? erhe::application::Item_mode::disabled
            : erhe::application::Item_mode::normal
    );
#endif
}

void Brushes::brush_palette(int& selected_brush_index)
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const std::size_t brush_count = m_brushes.size();
    const ImVec2 button_size{ImGui::GetContentRegionAvail().x, 0.0f};
    for (int i = 0; i < static_cast<int>(brush_count); ++i)
    {
        const auto brush = m_brushes[i];
        std::string label = fmt::format("{}##brush-{}", brush->name(), i); // TODO cache
        const bool button_pressed = erhe::application::make_button(
            label.c_str(),
            (selected_brush_index == i)
                ? erhe::application::Item_mode::active
                : erhe::application::Item_mode::normal,
            button_size
        );
        if (button_pressed)
        {
            selected_brush_index = i;
            m_brush = brush;
        }
    }
#endif
}

} // namespace editor
