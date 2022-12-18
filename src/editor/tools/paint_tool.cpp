// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "tools/paint_tool.hpp"

#include "editor_message_bus.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "windows/operations.hpp"

#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <fmt/core.h>
#include <fmt/format.h>

#include <glm/gtx/norm.hpp>

#include <string>

namespace editor
{

void Paint_vertex_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (m_paint_tool.try_ready())
    {
        set_ready(context);
    }
}

auto Paint_tool::try_ready() -> bool
{
    if (!is_enabled())
    {
        return false;
    }

    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr)
    {
        return false;
    }

    if (
        scene_view->get_hover(Hover_entry::tool_slot        ).valid ||
        scene_view->get_hover(Hover_entry::rendertarget_slot).valid
    )
    {
        return false;
    }

    return scene_view->get_hover(Hover_entry::content_slot).valid;
}

auto Paint_vertex_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (get_command_state() == erhe::application::State::Ready)
    {
        set_active(context);
    }

    if (get_command_state() != erhe::application::State::Active)
    {
        return false;
    }
    if (m_paint_tool.get_hover_scene_view() == nullptr)
    {
        set_inactive(context);
        return false;
    }

    m_paint_tool.paint();
    return true;
}

namespace {

auto vertex_id_from_corner_id(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const erhe::geometry::Geometry&           geometry,
    const erhe::geometry::Corner_id           corner_id
) -> std::optional<uint32_t>
{
    for (const auto& primitive : mesh->mesh_data.primitives)
    {
        if (primitive.source_geometry.get() == &geometry)
        {
            return primitive.gl_primitive_geometry.corner_to_vertex_id.at(corner_id);
        }
    }
    return std::nullopt;
}

}

void Paint_tool::paint_corner(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const erhe::geometry::Geometry&           geometry,
    erhe::geometry::Corner_id                 corner_id,
    const glm::vec4                           color
)
{
    const auto vertex_id_opt = vertex_id_from_corner_id(mesh, geometry, corner_id);
    if (!vertex_id_opt.has_value())
    {
        return;
    }
    paint_vertex(mesh, geometry, vertex_id_opt.value(), color);
}

void Paint_tool::paint_vertex(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const erhe::geometry::Geometry&           geometry,
    const uint32_t                            vertex_id,
    const glm::vec4                           color
)
{
    const auto&       vertex_format = m_mesh_memory->gl_vertex_format();
    const auto        attribute     = vertex_format.find_attribute(erhe::graphics::Vertex_attribute::Usage_type::color, 0);
    const std::size_t vertex_offset = vertex_id * vertex_format.stride() + attribute->offset;

    std::vector<std::uint8_t> buffer;
    buffer.resize(sizeof(float) * 4);
    auto* const ptr = reinterpret_cast<float*>(buffer.data());
    ptr[0] = color.x;
    ptr[1] = color.y;
    ptr[2] = color.z;
    ptr[3] = color.w;

    ERHE_VERIFY(attribute.get()->data_type.type == gl::Vertex_attrib_type::float_);

    for (const auto& primitive : mesh->mesh_data.primitives)
    {
        if (primitive.source_geometry.get() == &geometry)
        {
            const std::size_t range_byte_offset = primitive.gl_primitive_geometry.vertex_buffer_range.byte_offset;
            m_mesh_memory->gl_buffer_transfer_queue->enqueue(
                *m_mesh_memory->gl_vertex_buffer.get(),
                range_byte_offset + vertex_offset,
                std::move(buffer)
            );
            break;
        }
    }
}

void Paint_tool::paint()
{
    if (!is_enabled())
    {
        return;
    }

    m_point_id.reset();
    m_corner_id.reset();

    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr)
    {
        return;
    }

    const Hover_entry& content = scene_view->get_hover(Hover_entry::content_slot);
    if (
        !content.valid ||
        !content.mesh ||
        (content.geometry == nullptr) ||
        !content.position.has_value()
    )
    {
        return;
    }

    auto* const point_locations = content.geometry->point_attributes().find<glm::vec3>(erhe::geometry::c_point_locations);
    if (point_locations == nullptr)
    {
        return;
    }

    const erhe::geometry::Polygon_id polygon_id = static_cast<erhe::geometry::Polygon_id>(content.local_index);
    const erhe::geometry::Polygon&   polygon    = content.geometry->polygons.at(polygon_id);

    if (polygon.corner_count == 0)
    {
        return;
    }

    const glm::vec3 hover_position_in_world = content.position.value();

    const auto* node = content.mesh->get_node();
    if (node == nullptr)
    {
        return;
    }

    const glm::vec3 hover_position_in_mesh = node->transform_point_from_world_to_local(hover_position_in_world);

    float                     max_distance_squared = std::numeric_limits<float>::max();
    erhe::geometry::Point_id  nearest_point_id     = 0;
    erhe::geometry::Corner_id nearest_corner_id    = 0;
    polygon.for_each_corner_const(
        *content.geometry,
        [&](const erhe::geometry::Polygon::Polygon_corner_context_const& i)
        {
            const erhe::geometry::Point_id point_id = i.corner.point_id;
            const glm::vec3 p = point_locations->get(point_id);
            const float d2 = glm::distance2(hover_position_in_mesh, p);
            if (d2 < max_distance_squared)
            {
                max_distance_squared = d2;
                nearest_point_id = point_id;
                nearest_corner_id = i.corner_id;
            }
        }
    );

    m_point_id  = nearest_point_id;
    m_corner_id = nearest_corner_id;

    switch (m_paint_mode)
    {
        case Paint_mode::Corner:
        {
            paint_corner(content.mesh, *content.geometry, nearest_corner_id, m_color);
            break;
        }
        case Paint_mode::Point:
        {
            const erhe::geometry::Corner& corner = content.geometry->corners.at(nearest_corner_id);
            const erhe::geometry::Point&  point  = content.geometry->points.at(corner.point_id);
            //const uint32_t vertex_id = content.p
            point.for_each_corner_const(*content.geometry, [&](const auto& i){
                paint_corner(content.mesh, *content.geometry, i.corner_id, m_color);
            });
            break;
        }
        case Paint_mode::Polygon:
        {
            polygon.for_each_corner_const(
                *content.geometry,
                [&](const erhe::geometry::Polygon::Polygon_corner_context_const& i)
                {
                    paint_corner(content.mesh, *content.geometry, i.corner_id, m_color);
                }
            );
            break;
        }
    }
}

Paint_tool::Paint_tool()
    : erhe::application::Imgui_window{c_title}
    , erhe::components::Component    {c_type_name}
    , m_paint_vertex_command         {*this}
{
}

Paint_tool::~Paint_tool() noexcept
{
}

auto Paint_tool::description() -> const char*
{
    return c_title.data();
}

void Paint_tool::declare_required_components()
{
    require<erhe::application::Commands     >();
    require<erhe::application::Imgui_windows>();
    require<Editor_message_bus>();
    require<Operations        >();
    require<Tools             >();
}

void Paint_tool::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const auto& imgui_windows = get<erhe::application::Imgui_windows>();
    imgui_windows->register_imgui_window(this);

    const auto commands = get<erhe::application::Commands>();
    commands->register_command(&m_paint_vertex_command);
    commands->bind_command_to_mouse_drag             (&m_paint_vertex_command, erhe::toolkit::Mouse_button_right);
    commands->bind_command_to_controller_trigger_drag(&m_paint_vertex_command);
    get<Operations>()->register_active_tool(this);
    get<Tools     >()->register_tool(this);

#if 0
    m_ngon_colors.emplace_back(240.0f / 255.0f, 163.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f, 117.0f / 255.0f, 220.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(153.0f / 255.0f,  63.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back( 76.0f / 255.0f,   0.0f / 255.0f,  92.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back( 25.0f / 255.0f,  25.0f / 255.0f,  25.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f,  92.0f / 255.0f,  49.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back( 43.0f / 255.0f, 206.0f / 255.0f,  72.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 204.0f / 255.0f, 153.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(148.0f / 255.0f, 255.0f / 255.0f, 181.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(143.0f / 255.0f, 124.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(157.0f / 255.0f, 204.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(194.0f / 255.0f,   0.0f / 255.0f, 136.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f,  51.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 164.0f / 255.0f,   5.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 168.0f / 255.0f, 187.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back( 66.0f / 255.0f, 102.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f,   0.0f / 255.0f,  16.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back( 94.0f / 255.0f, 241.0f / 255.0f, 242.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f, 153.0f / 255.0f, 143.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(224.0f / 255.0f, 255.0f / 255.0f, 102.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(116.0f / 255.0f,  10.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(153.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 255.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 255.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f,  80.0f / 255.0f,   5.0f / 255.0f, 1.0f);
#endif

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

    get<Editor_message_bus>()->add_receiver(
        [&](Editor_message& message)
        {
            Tool::on_message(message);
        }
    );

}

void Paint_tool::post_initialize()
{
    m_viewport_windows = get<Viewport_windows>();
    m_mesh_memory      = get<Mesh_memory     >();
}

void Paint_tool::imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    ImGui::ColorEdit4("Color", &m_color.x, ImGuiColorEditFlags_Float);

    ImGui::SetNextItemWidth(200);
    erhe::application::make_combo(
        "Paint mode",
        m_paint_mode,
        c_paint_mode_strings,
        IM_ARRAYSIZE(c_paint_mode_strings)
    );

    if (m_point_id.has_value())
    {
        ImGui::Text("Point: %u", m_point_id.value());
    }
    if (m_corner_id.has_value())
    {
        ImGui::Text("Corner: %u", m_corner_id.value());
    }

    int corner_count = 3;
    for (auto& ngon_color : m_ngon_colors)
    {
        std::string label = fmt::format("{}-gon", corner_count);
        ImGui::ColorEdit4(label.c_str(), &ngon_color.x, ImGuiColorEditFlags_Float);
        ++corner_count;
    }
    std::string label = fmt::format("{}-gon", corner_count);
    if (ImGui::Button(label.c_str(), button_size))
    {
        m_ngon_colors.push_back(glm::vec4{1.0f, 1.0f, 1.0f, 1.0f});
    }
    if (ImGui::Button("Color Selection") && !m_ngon_colors.empty())
    {
        const auto& selection_tool = get<Selection_tool>();
        const auto& selection = selection_tool->selection();
        for (const auto& node : selection)
        {
            const auto& mesh = as_mesh(node);
            if (!mesh)
            {
                continue;
            }
            for (const auto& primitive : mesh->mesh_data.primitives)
            {
                primitive.source_geometry->for_each_polygon_const(
                    [&](const auto& i)
                    {
                        const std::size_t color_index = std::min(
                            static_cast<std::size_t>(i.polygon.corner_count),
                            m_ngon_colors.size() - 1
                        );

                        const glm::vec4 color = m_ngon_colors.at(color_index);
                        i.polygon.for_each_corner_const(
                            *primitive.source_geometry.get(),
                            [&](const erhe::geometry::Polygon::Polygon_corner_context_const& i)
                            {
                                paint_corner(mesh, *primitive.source_geometry, i.corner_id, color);
                            }
                        );
                    }
                );
            }
        }
    }
}

} // namespace editor
