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
    class Build_info;
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
    class Parameters
    {
    public:
        erhe::primitive::Build_info& build_info;
        erhe::scene::Mesh_layer&     layer;
        erhe::scene::Scene&          scene;
        erhe::physics::IWorld&       physics_world;
        erhe::raytrace::IScene*      raytrace_scene{nullptr};
        Selection_tool*              selection_tool{nullptr};
    };

    explicit Merge_operation(Parameters&& parameters);

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

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
        float                                            mass         {0.0f};
        glm::mat4                                        local_inertia{0.0f};
        erhe::physics::Transform                         rigidbody_from_node;
        std::vector<std::shared_ptr<erhe::scene::Node>>  selection;
    };

    Parameters                                                  m_parameters;
    std::shared_ptr<erhe::scene::Node>                          m_parent;
    std::vector<Source_entry>                                   m_source_entries;
    erhe::primitive::Primitive                                  m_combined_primitive;
    State_data                                                  m_state_before;
    State_data                                                  m_state_after;
    std::vector<std::shared_ptr<erhe::scene::Node>>             m_hold_nodes;
    std::vector<std::shared_ptr<erhe::scene::INode_attachment>> m_hold_node_attachments;
};

}
