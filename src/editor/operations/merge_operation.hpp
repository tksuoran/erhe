#pragma once

#include "operations/ioperation.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include <vector>

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
        std::shared_ptr<Selection_tool>  selection_tool;
    };

    explicit Merge_operation(Context& context);

    // Implements IOperation
    void execute() override;
    void undo   () override;

private:
    class Source_entry
    {
    public:
        Source_entry(
            erhe::primitive::Build_info_set&               build_info_set,
            erhe::scene::Scene&                            scene,
            erhe::scene::Mesh_layer&                       layer,
            erhe::physics::IWorld&                         physics_world,
            std::shared_ptr<erhe::scene::Mesh>             mesh,
            const std::vector<erhe::primitive::Primitive>& primitives
        )
            : build_info_set{build_info_set}
            , scene         {scene         }
            , layer         {layer         }
            , physics_world {physics_world }
            , mesh          {mesh          }
            , primitives    {primitives    }
        {
        }

        erhe::primitive::Build_info_set&        build_info_set;
        erhe::scene::Scene&                     scene;
        erhe::scene::Mesh_layer&                layer;
        erhe::physics::IWorld&                  physics_world;
        std::shared_ptr<erhe::scene::Mesh>      mesh;
        std::vector<erhe::primitive::Primitive> primitives;
    };

    class State_data
    {
    public:
        std::shared_ptr<erhe::physics::ICollision_shape> collision_shape;
        float                                            mass;
        glm::vec3                                        local_inertia;
        glm::mat3                                        center_of_mass_transform_basis;
        glm::vec3                                        center_of_mass_transform_origin;
        std::vector<std::shared_ptr<erhe::scene::Node>>  selection;
    };

    Context                                              m_context;
    erhe::scene::Node*                                   m_parent{nullptr};
    std::vector<Source_entry>                            m_source_entries;
    std::shared_ptr<erhe::primitive::Primitive_geometry> m_combined_primitive_geometry;
    State_data                                           m_state_before;
    State_data                                           m_state_after;
    std::vector<std::shared_ptr<erhe::scene::Node>>      m_hold_nodes;
};

}
