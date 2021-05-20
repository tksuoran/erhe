#include "windows/brushes.hpp"
#include "editor.hpp"
#include "log.hpp"
#include "operations/operation_stack.hpp"
#include "operations/insert_operation.hpp"
#include "renderers/line_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "tools/grid_tool.hpp"
#include "tools/pointer_context.hpp"
#include "erhe/geometry/operation/clone.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/mesh.hpp"

#include "imgui.h"

#include <glm/gtx/transform.hpp>

namespace editor
{

using namespace std;
using namespace glm;
using namespace erhe::geometry;
using erhe::geometry::Polygon; // Resolve conflict with wingdi.h BOOL Polygon(HDC,const POINT *,int)
using namespace erhe::primitive;
using namespace erhe::scene;
using namespace erhe::toolkit;

auto Brushes::state() const -> Tool::State
{
    return m_state;
}

void Brushes::connect()
{
    m_operation_stack = get<Operation_stack>();
    m_scene_manager   = require<Scene_manager>();
    m_selection_tool  = get<Selection_tool>();
    m_grid_tool       = get<Grid_tool>();
}

void Brushes::initialize_component()
{
    const auto& materials = m_scene_manager->materials();
    for (auto material : materials)
    {
        add_material(material);
    }

    m_brushes.push_back(Brush());
    m_brush_names.push_back("-");
    const auto& primitive_geometries = m_scene_manager->primitive_geometries();
    for (auto primitive_geometry : primitive_geometries)
    {
        add_brush(primitive_geometry);
    }
}

Brush::Brush(const std::shared_ptr<erhe::primitive::Primitive_geometry>& primitive_geometry)
    : primitive_geometry{primitive_geometry}
{
}

void Brushes::add_brush(const shared_ptr<Primitive_geometry>& primitive_geometry)
{
    m_brushes.emplace_back(Brush(primitive_geometry));
    auto* geometry = primitive_geometry->source_geometry.get();
    const char* name = (geometry != nullptr) ? geometry->name.c_str() : "";
    m_brush_names.push_back(name);
}

void Brushes::add_material(const shared_ptr<Material>& material)
{
    m_materials.push_back(material);
    m_material_names.push_back(material->name.c_str());
}

void Brushes::cancel_ready()
{
    m_state = State::passive;
    if (m_brush_mesh)
    {
        remove_hover_mesh();
    }
}

auto Brushes::update(Pointer_context& pointer_context) -> bool
{
    if (pointer_context.priority_action != Action::add)
    {
        if (m_brush_mesh)
        {
            remove_hover_mesh();
        }
        return false;
    }

    m_hover_content     = pointer_context.hover_content;
    m_hover_tool        = pointer_context.hover_tool;
    m_hover_node        = pointer_context.hover_mesh ? pointer_context.hover_mesh->node : shared_ptr<Node>{};
    m_hover_primitive   = pointer_context.hover_primitive;
    m_hover_local_index = pointer_context.hover_local_index;
    m_hover_geometry    = pointer_context.geometry;
    m_hover_position    = m_hover_content && pointer_context.hover_valid ? pointer_context.position_in_world() : optional<vec3>{};
    m_hover_normal      = m_hover_content && pointer_context.hover_valid ? pointer_context.hover_normal        : optional<vec3>{};
    if (m_hover_node && m_hover_position.has_value())
    {
        m_hover_position = m_hover_node->node_from_world() * vec4(m_hover_position.value(), 1.0f);
    }

    if ((m_state == State::passive) &&
        pointer_context.mouse_button[Mouse_button_left].pressed &&
        m_brush_mesh)
    {
        m_state = State::ready;
        return true;
    }

    if (m_state != State::ready)
    {
        return false;
    }

    if (pointer_context.mouse_button[Mouse_button_left].released && m_brush_mesh)
    {
        do_insert_operation();
        remove_hover_mesh();
        m_state = State::passive;
        return true;
    }

    return false;
}

void Brushes::render(Render_context&)
{
}

void Brushes::render_update()
{
    update_mesh();
}

Reference_frame::Reference_frame(const erhe::geometry::Geometry& geometry,
                                 erhe::geometry::Polygon_id      polygon_id)
    : polygon_id(polygon_id)
{
    auto* polygon_centroids = geometry.polygon_attributes().find<vec3>(c_polygon_centroids);
    auto* polygon_normals   = geometry.polygon_attributes().find<vec3>(c_polygon_normals);
    auto* point_locations   = geometry.point_attributes().find<vec3>(c_point_locations);
    VERIFY(point_locations != nullptr);

    const Polygon& polygon = geometry.polygons[polygon_id];
    centroid = (polygon_centroids != nullptr) && polygon_centroids->has(polygon_id)
        ? polygon_centroids->get(polygon_id)
        : polygon.compute_centroid(geometry, *point_locations);
    N = (polygon_normals != nullptr) && polygon_normals->has(polygon_id)
        ? polygon_normals->get(polygon_id)
        : polygon.compute_normal(geometry, *point_locations);

    Corner_id     corner_id = geometry.polygon_corners[polygon.first_polygon_corner_id];
    const Corner& corner    = geometry.corners[corner_id];
    Point_id      point     = corner.point_id;
    VERIFY(point_locations->has(point));
    position = point_locations->get(point);
    vec3 midpoint = polygon.compute_edge_midpoint(geometry, *point_locations);
    T = normalize(midpoint - centroid);
    B = normalize(cross(N, T));
    N = normalize(cross(T, B));
    T = normalize(cross(B, N));
    corner_count = polygon.corner_count;
}

void Reference_frame::transform_by(mat4 m)
{
    centroid = m * vec4(centroid, 1.0f);
    position = m * vec4(position, 1.0f);
    B = m * vec4(B, 0.0f);
    T = m * vec4(T, 0.0f);
    N = m * vec4(N, 0.0f);
    B = normalize(cross(N, T));
    N = normalize(cross(T, B));
    T = normalize(cross(B, N));
}

auto Reference_frame::scale() const -> float
{
    return glm::distance(centroid, position);
}

auto Reference_frame::transform() const -> mat4
{
    vec3 C = centroid;
    return mat4(vec4(B, 0.0f),
                vec4(T, 0.0f),
                vec4(N, 0.0f),
                vec4(centroid, 1.0f));
}

auto Brush::get_reference_frame(uint32_t corner_count)
-> Reference_frame
{
    VERIFY(primitive_geometry->source_geometry != nullptr);
    const auto& geometry = *primitive_geometry->source_geometry;

    for (const auto& reference_frame : reference_frames)
    {
        if (reference_frame.corner_count == corner_count)
        {
            return reference_frame;
        }
    }

    for (Polygon_id polygon_id = 0, end = geometry.polygon_count();
         polygon_id < end;
         ++polygon_id)
    {
        const Polygon& polygon = geometry.polygons[polygon_id];
        if (polygon.corner_count == corner_count || (polygon_id + 1 == end))
        {
            return reference_frames.emplace_back(geometry, polygon_id);
        }
    }

    log_brush.error("{} invalid code path\n", __func__);
    return Reference_frame{};
}

auto Brush::get_scaled_primitive_geometry(float scale, Scene_manager& scene_manager)
-> std::shared_ptr<erhe::primitive::Primitive_geometry>
{
    int scale_key = static_cast<int>(scale * c_scale_factor);
    for (const auto& scaled : scaled_primitive_geometries)
    {
        if (scaled.scale_key == scale_key)
        {
            return scaled.primitive_geometry;
        }
    }
    auto primitive_geometry = create_scaled(scale_key, scene_manager);
    scaled_primitive_geometries.emplace_back(scale_key, primitive_geometry);
    return primitive_geometry;
}

auto Brush::create_scaled(int scale_key, Scene_manager& scene_manager)
-> std::shared_ptr<erhe::primitive::Primitive_geometry>
{
    VERIFY(primitive_geometry);
    float scale = static_cast<float>(scale_key) / c_scale_factor;
    mat4 scale_transform = erhe::toolkit::create_scale(scale);
    Geometry scaled_geometry = erhe::geometry::operation::clone(*primitive_geometry->source_geometry.get());
    scaled_geometry.transform(scale_transform);
    return scene_manager.make_primitive_geometry(std::move(scaled_geometry),
                                                 erhe::primitive::Primitive_geometry::Normal_style::polygon_normals);
}

auto Brushes::get_brush_transform() -> mat4
{
    if ((m_hover_node == nullptr) || (m_hover_geometry == nullptr))
    {
        return mat4(1);
    }

    Polygon_id      polygon_id = static_cast<Polygon_id>(m_hover_local_index);
    const Polygon&  polygon    = m_hover_geometry->polygons[polygon_id];
    Brush&          brush      = m_brushes[m_selected_brush];
    Reference_frame hover_frame(*m_hover_geometry, polygon_id);
    Reference_frame brush_frame = brush.get_reference_frame(polygon.corner_count);
    hover_frame.N *= -1.0f;
    hover_frame.B *= -1.0f;

    VERIFY(brush_frame.scale() != 0.0f);

    float scale = hover_frame.scale() / brush_frame.scale();
    m_transform_scale = scale;
    if (scale != 1.0f)
    {
        mat4 scale_transform = erhe::toolkit::create_scale(scale);
        brush_frame.transform_by(scale_transform);
    }

    if (!m_snap_to_hover_polygon && m_hover_position.has_value())
    {
        hover_frame.centroid = m_hover_position.value();
        if (m_snap_to_grid)
        {
            hover_frame.centroid = m_grid_tool->snap(hover_frame.centroid);
        }
    }

    mat4 hover_transform = hover_frame.transform();
    mat4 brush_transform = brush_frame.transform();
    mat4 inverse_brush   = inverse(brush_transform);
    mat4 align           = hover_transform * inverse_brush;

    return align;
}

void Brushes::update_mesh_node_transform()
{
    if (!m_hover_position.has_value())
    {
        return;
    }
    auto node = m_brush_mesh->node;
    if (node == nullptr)
    {
        // unexpected
        return;
    }

    auto transform = get_brush_transform();
    node->parent = m_hover_node.get(); // TODO reference count
    node->transforms.parent_from_node.set(transform);
    m_brush_mesh->primitives.front().primitive_geometry = get_brush_primitive_geometry();
}

void Brushes::do_insert_operation()
{
    if (!m_hover_position.has_value())
    {
        return;
    }

    log_brush.trace("{} scale = {}\n", __func__, m_transform_scale);

    auto   transform          = get_brush_transform();
    Brush& brush              = m_brushes[m_selected_brush];
    auto   primitive_geometry = get_brush_primitive_geometry();
    auto   material           = m_materials[m_selected_material];

    auto node = make_shared<Node>();
    node->parent = m_hover_node.get(); // TODO reference count
    node->transforms.parent_from_node.set(transform);
    node->update();

    auto mesh = make_shared<Mesh>(primitive_geometry->source_geometry->name, node);
    node->reference_count++;
    mesh->primitives.emplace_back(primitive_geometry, material);
    mesh->node->reference_count++;

    Mesh_insert_remove_operation::Context context;
    context.item           = mesh;
    context.mode           = Scene_item_operation::Mode::insert;
    context.scene_manager  = m_scene_manager;
    context.selection_tool = m_selection_tool;

    auto op = make_shared<Mesh_insert_remove_operation>(context);
    m_operation_stack->push(op);
}

auto Brushes::get_brush_primitive_geometry()
-> std::shared_ptr<erhe::primitive::Primitive_geometry>
{
    Brush& brush = m_brushes[m_selected_brush];
    //return brush.primitive_geometry;
    return brush.get_scaled_primitive_geometry(m_transform_scale, *m_scene_manager.get());
}

void Brushes::add_hover_mesh()
{
    if ((m_selected_brush == 0) || !m_hover_position.has_value())
    {
        return;
    }
    string name     = m_brush_names[m_selected_brush];
    auto   material = m_materials[m_selected_material];
    m_brush_mesh = m_scene_manager->make_mesh_node(name, get_brush_primitive_geometry(), material);
    m_brush_mesh->visibility_mask &= ~(Mesh::c_visibility_id);
    update_mesh_node_transform();
}

void Brushes::remove_hover_mesh()
{
    auto layer = m_scene_manager->content_layer();
    auto& meshes = layer->meshes;
    auto i = remove(meshes.begin(), meshes.end(), m_brush_mesh);
    if (i != meshes.end())
    {
        meshes.erase(i, meshes.end());
    }
    m_brush_mesh.reset();
}

void Brushes::update_mesh()
{
    if (!m_brush_mesh)
    {
        add_hover_mesh();
        return;
    }

    if ((m_selected_brush == 0) || !m_hover_position.has_value())
    {
        remove_hover_mesh();
        return;
    }

    update_mesh_node_transform();
}

void Brushes::window(Pointer_context&)
{
    ImGui::Begin("Brushes");
    size_t brush_count = m_brush_names.size();
    auto button_size = ImVec2(ImGui::GetContentRegionAvailWidth(), 0.0f);
    for (int i = 0; i < static_cast<int>(brush_count); ++i)
    {
        bool button_pressed = make_button(m_brush_names[i],
                                          (m_selected_brush == i) ? Item_mode::active
                                                                  : Item_mode::normal,
                                          button_size);
        if (button_pressed)
        {
            m_selected_brush = i;
        }
    }
    bool false_value = false;
    ImGui::SliderFloat("Scale", &m_scale, 0.0f, 2.0f);
    make_check_box("Snap to Polygon", &m_snap_to_hover_polygon);
    make_check_box("Snap to Grid", &m_snap_to_grid, m_snap_to_hover_polygon ? Window::Item_mode::disabled
                                                                            : Window::Item_mode::normal);
    ImGui::End();

    ImGui::Begin("Materials");
    if (!m_material_names.empty())
    {
        size_t material_count = m_material_names.size();
        auto button_size = ImVec2(ImGui::GetContentRegionAvailWidth(), 0.0f);
        for (int i = 0; i < static_cast<int>(material_count); ++i)
        {
            bool button_pressed = make_button(m_material_names[i],
                                              (m_selected_material == i) ? Item_mode::active
                                                                         : Item_mode::normal,
                                              button_size);
            if (button_pressed)
            {
                m_selected_material = i;
            }
        }
    }
    ImGui::End();
}

}
