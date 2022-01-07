#pragma once

#include "operations/ioperation.hpp"

#include "erhe/physics/transform.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"

namespace erhe::physics
{
    class ICollision_shape;
    class IWorld;
};

namespace erhe::primitive
{
    class Build_info_set;
    class Primitive_geometry;
};

namespace erhe::raytrace
{
    class IScene;
    class IGeometry;
};

namespace erhe::scene
{
    class Mesh_layer;
    class Scene;
};

namespace editor
{

class Scene_root;
class Selection_tool;

class Merge_operation
    : public IOperation
{
public:
    class Context
    {
    public:
        erhe::primitive::Build_info_set& build_info_set;
        erhe::scene::Mesh_layer&         layer;
        erhe::scene::Scene&              scene;
        erhe::physics::IWorld&           physics_world;
        erhe::raytrace::IScene*          raytrace_scene{nullptr};
        Selection_tool*                  selection_tool{nullptr};
    };

    explicit Merge_operation(Context&& context);

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute () const override;
    void undo    () const override;

private:
    class Source_entry
    {
    public:
        Source_entry(
            const std::shared_ptr<erhe::scene::Mesh>&      mesh,
            const std::vector<erhe::primitive::Primitive>& primitives
        )
        :   mesh      {mesh}
        ,   primitives{primitives}
        {
        }

        std::shared_ptr<erhe::scene::Mesh>      mesh;
        std::vector<erhe::primitive::Primitive> primitives;
    };

    class State_data
    {
    public:
        std::shared_ptr<erhe::physics::ICollision_shape> collision_shape;
        float                                            mass         {1.0f};
        glm::vec3                                        local_inertia{1.0f};
        erhe::physics::Transform                         rigidbody_from_node;
        std::vector<std::shared_ptr<erhe::scene::Node>>  selection;
    };

    Context                                                     m_context;
    erhe::scene::Node*                                          m_parent{nullptr};
    std::vector<Source_entry>                                   m_source_entries;
    erhe::primitive::Primitive                                  m_combined_primitive;
    State_data                                                  m_state_before;
    State_data                                                  m_state_after;
    std::vector<std::shared_ptr<erhe::scene::Node>>             m_hold_nodes;
    std::vector<std::shared_ptr<erhe::scene::INode_attachment>> m_hold_node_attachments;
};

}
