#include "operations/merge_operation.hpp"

#include "editor_log.hpp"
#include "operations/merge_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/operation/weld.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/defer.hpp"

#include <memory>
#include <sstream>

namespace editor
{

auto Merge_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Merge ";
    bool first = true;
    for (const auto& entry : m_sources)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            ss << ", ";
        }
        ss << entry.mesh->get_name();
    }
    return ss.str();
}

Merge_operation::Merge_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    // TODO count meshes in selection
    if (g_selection_tool->selection().size() < 2)
    {
        return;
    }

    using erhe::primitive::Normal_style;
    using glm::mat4;

    erhe::physics::Compound_shape_create_info  compound_shape_create_info;
    erhe::geometry::Geometry                   combined_geometry;
    std::shared_ptr<erhe::primitive::Material> material;
    Scene_root* scene_root                = nullptr;
    bool        first_mesh                = true;
    mat4        reference_node_from_world = mat4{1};
    auto        normal_style              = Normal_style::none;

    ERHE_VERIFY(g_selection_tool != nullptr);

    m_selection_before = g_selection_tool->selection();

    for (const auto& item : g_selection_tool->selection())
    {
        const auto& mesh = as_mesh(item);
        if (!mesh)
        {
            continue;
        }

        auto* node = mesh->get_node();
        if (node == nullptr)
        {
            continue;
        }

        mat4 transform;
        const auto node_physics  = get_node_physics(node);
        const auto node_raytrace = get_raytrace    (node);

        //rt_primitive = std::make_shared<Raytrace_primitive>(geometry);
        Entry source_entry{
            .mesh          = mesh,
            .node          = std::static_pointer_cast<erhe::scene::Node>(node->shared_from_this()),
            .before_parent = node->parent().lock(),
            .node_physics  = node_physics,
            .node_raytrace = node_raytrace
        };

        ERHE_VERIFY(source_entry.before_parent);

        if (first_mesh)
        {
            scene_root                = reinterpret_cast<Scene_root*>(node->node_data.host);
            reference_node_from_world = node->node_from_world();
            transform                 = mat4{1};
            first_mesh                = false;
            m_selection_after.push_back(mesh);
            m_combined.mesh = mesh;
        }
        else
        {
            transform = reference_node_from_world * node->world_from_node();
        }

        if (node_physics)
        {
            auto* rigid_body = node_physics->rigid_body();
            if (rigid_body != nullptr)
            {
                auto collision_shape = rigid_body->get_collision_shape();

                erhe::physics::Compound_child child{
                    .shape     = collision_shape,
                    .transform = erhe::physics::Transform{transform}
                };
                compound_shape_create_info.children.push_back(child);
            }
        }

        for (auto& primitive : mesh->mesh_data.primitives)
        {
            const auto& geometry = primitive.source_geometry;
            if (!geometry)
            {
                continue;
            }
            combined_geometry.merge(*geometry, transform);
            source_entry.primitives.push_back(primitive);
            if (normal_style == Normal_style::none)
            {
                normal_style = primitive.normal_style;
            }
            if (!material)
            {
                material = primitive.material;
            }
        }

        m_sources.emplace_back(source_entry);
    }

    if (m_sources.size() < 2)
    {
        m_sources.clear();
        return;
    }

    if (!compound_shape_create_info.children.empty())
    {
        ERHE_VERIFY(scene_root != nullptr);
        const auto& combined_collision_shape = erhe::physics::ICollision_shape::create_compound_shape_shared(
            compound_shape_create_info
        );
        auto& physics_world = scene_root->physics_world();

        const erhe::physics::IRigid_body_create_info rigid_body_create_info{
            .world           = physics_world,
            .collision_shape = combined_collision_shape,
            .debug_label     = "merged" // TODO
        };

        m_combined.node_physics = std::make_shared<Node_physics>(rigid_body_create_info);
    }

    std::shared_ptr<erhe::geometry::Geometry> welded_geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::operation::weld(combined_geometry)
    );;

    m_combined.node_raytrace = std::make_shared<Node_raytrace>(welded_geometry);
    const auto* rt_primitive = m_combined.node_raytrace->raytrace_primitive();

    m_combined.primitives.push_back(
        erhe::primitive::Primitive{
            .material              = material,
            .gl_primitive_geometry = make_primitive(
                *welded_geometry.get(),
                parameters.build_info,
                normal_style
            ),
            .rt_primitive_geometry = rt_primitive->primitive_geometry,
            .rt_vertex_buffer      = rt_primitive->vertex_buffer,
            .rt_index_buffer       = rt_primitive->index_buffer,
            .source_geometry       = welded_geometry,
            .normal_style          = normal_style
        }
    );
}

void Merge_operation::execute(const Operation_context&)
{
    log_operations->trace("begin Op Execute {}", describe());

    if (m_sources.empty())
    {
        return;
    }

    auto* const scene_root = reinterpret_cast<Scene_root*>(m_sources.front().node->node_data.host);
    if (scene_root == nullptr)
    {
        return;
    }
    auto& scene = scene_root->scene();

    scene.sanity_check();

    bool first_entry = true;
    for (const auto& entry : m_sources)
    {
        const auto& mesh = entry.mesh;

        if (entry.node == nullptr)
        {
            continue;
        }

        erhe::scene::Node* node = entry.node.get();

        if (first_entry)
        {
            // For first mesh: Replace mesh primitives
            mesh->mesh_data.primitives = m_combined.primitives;

            auto old_mesh = erhe::scene::get_mesh(node);
            if (old_mesh)
            {
                node->detach(old_mesh.get());
            }
            if (entry.mesh)
            {
                node->attach(m_combined.mesh);
            }

            auto old_node_physics = get_node_physics(node);
            if (old_node_physics)
            {
                node->detach(old_node_physics.get());
            }
            if (m_combined.node_physics)
            {
                node->attach(m_combined.node_physics);
            }

            auto old_node_raytrace = get_raytrace(node);
            if (old_node_raytrace)
            {
                node->detach(old_node_raytrace.get());
            }
            if (m_combined.node_raytrace)
            {
                node->attach(m_combined.node_raytrace);
            }

            first_entry = false;
        }
        else
        {
            entry.node->set_parent({});
        }
    }
    g_selection_tool->set_selection(m_selection_after);

    scene.sanity_check();
    log_operations->trace("end Op Execute {}", describe());
}

void Merge_operation::undo(const Operation_context&)
{
    log_operations->trace("Op Undo {}", describe());

    if (m_sources.empty())
    {
        return;
    }

    auto* const scene_root = reinterpret_cast<Scene_root*>(m_sources.front().node->node_data.host);
    if (scene_root == nullptr)
    {
        return;
    }

    auto& scene = scene_root->scene();
    scene.sanity_check();

    bool first_entry = true;
    for (const auto& entry : m_sources)
    {
        // For all entries:
        auto& mesh = entry.mesh;
        mesh->mesh_data.primitives = entry.primitives;

        erhe::scene::Node* node = entry.node.get();

        if (first_entry)
        {
            first_entry = false;

            auto old_mesh = erhe::scene::get_mesh(node);
            if (old_mesh)
            {
                node->detach(old_mesh.get());
            }
            if (entry.mesh)
            {
                node->attach(entry.mesh);
            }

            auto old_node_physics = get_node_physics(node);
            if (old_node_physics)
            {
                node->detach(old_node_physics.get());
            }
            if (entry.node_physics)
            {
                node->attach(entry.node_physics);
            }

            auto old_node_raytrace = get_raytrace(node);
            if (old_node_raytrace)
            {
                node->detach(old_node_physics.get());
            }
            if (entry.node_raytrace)
            {
                node->attach(entry.node_raytrace);
            }
        }
        else
        {
            node->set_parent(entry.before_parent);
        }
    }
    g_selection_tool->set_selection(m_selection_before);

    scene.sanity_check();
}

} // namespace editor
