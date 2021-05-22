#include "windows/brushes.hpp"
#include "editor.hpp"
#include "log.hpp"
#include "operations/operation_stack.hpp"
#include "operations/insert_operation.hpp"
#include "renderers/line_renderer.hpp"
#include "scene/brush.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_manager.hpp"
#include "tools/grid_tool.hpp"
#include "tools/pointer_context.hpp"
#include "erhe/geometry/operation/clone.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"

#include "imgui.h"
#include "BulletCollision/CollisionShapes/btUniformScalingShape.h"

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

    auto& brushes = m_scene_manager->brushes();
    m_selected_brush_index = 0;
    m_brush = &brushes.front();
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
    m_hover_node        = pointer_context.hover_mesh ? pointer_context.hover_mesh->node() : shared_ptr<Node>{};
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

auto Brushes::get_brush_transform() -> mat4
{
    if ((m_hover_node == nullptr) || (m_hover_geometry == nullptr) || (m_brush == nullptr))
    {
        return mat4(1);
    }

    Polygon_id      polygon_id = static_cast<Polygon_id>(m_hover_local_index);
    const Polygon&  polygon    = m_hover_geometry->polygons[polygon_id];
    Reference_frame hover_frame(*m_hover_geometry, polygon_id);
    Reference_frame brush_frame = m_brush->get_reference_frame(polygon.corner_count);
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

    debug_info.hover_frame_scale = hover_frame.scale();
    debug_info.brush_frame_scale = brush_frame.scale();
    debug_info.transform_scale   = scale;

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
    auto node = m_brush_mesh->node();
    if (node == nullptr)
    {
        // unexpected
        log_brush.warn("brush mesh has no node");
        return;
    }

    auto  transform    = get_brush_transform();
    auto& brush_scaled = m_brush->get_scaled(m_transform_scale, *m_scene_manager.get());
    node->parent = m_hover_node.get(); // TODO reference count
    node->transforms.parent_from_node.set(transform);
    m_brush_mesh->primitives.front().primitive_geometry = brush_scaled.primitive_geometry;
}

void Brushes::do_insert_operation()
{
    if (!m_hover_position.has_value() || (m_brush == nullptr))
    {
        return;
    }

    log_brush.trace("{} scale = {}\n", __func__, m_transform_scale);

    auto transform = get_brush_transform();
    auto material  = m_materials[m_selected_material];
    auto instance  = m_brush->make_instance(*m_scene_manager.get(),
                                            m_hover_node,
                                            transform,
                                            material,
                                            m_transform_scale);
    Mesh_insert_remove_operation::Context context;
    context.mode           = Scene_item_operation::Mode::insert;
    context.mesh           = instance.mesh;
    context.node           = instance.node;
    context.node_physics   = instance.node_physics;
    context.scene_manager  = m_scene_manager;
    context.selection_tool = m_selection_tool;

    auto op = make_shared<Mesh_insert_remove_operation>(context);
    m_operation_stack->push(op);
}

void Brushes::add_hover_mesh()
{
    if ((m_brush == nullptr) || !m_hover_position.has_value())
    {
        return;
    }

    auto  transform    = get_brush_transform();
    auto  material     = m_materials[m_selected_material];
    auto& brush_scaled = m_brush->get_scaled(m_transform_scale, *m_scene_manager.get());
    m_brush_mesh = m_scene_manager->make_mesh_node(brush_scaled.primitive_geometry->source_geometry->name,
                                                   brush_scaled.primitive_geometry,
                                                   material);
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

    if ((m_brush == nullptr) || !m_hover_position.has_value())
    {
        remove_hover_mesh();
        return;
    }

    update_mesh_node_transform();
}

void Brushes::window(Pointer_context&)
{
    ImGui::Begin("Brushes");

    ImGui::InputFloat("Hover scale",     &debug_info.hover_frame_scale);
    ImGui::InputFloat("Brush scale",     &debug_info.brush_frame_scale);
    ImGui::InputFloat("Transform scale", &debug_info.transform_scale);

    auto&  brushes     = m_scene_manager->brushes();
    size_t brush_count = brushes.size();

    auto button_size = ImVec2(ImGui::GetContentRegionAvailWidth(), 0.0f);
    for (int i = 0; i < static_cast<int>(brush_count); ++i)
    {
        auto& brush = brushes[i];
        bool button_pressed = make_button(brush.geometry->name.c_str(),
                                          (m_selected_brush_index == i) ? Item_mode::active
                                                                        : Item_mode::normal,
                                          button_size);
        if (button_pressed)
        {
            m_selected_brush_index = i;
            m_brush = &brush;
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
