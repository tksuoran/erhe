#pragma once

#include "operations/ioperation.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include <vector>

namespace erhe::physics
{
    class IWorld;
};

namespace erhe::primitive
{
    class Build_info_set;
    class Primitive_geometry;
};

namespace erhe::scene
{
    class Layer;
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
        erhe::scene::Layer&              layer;
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
            erhe::scene::Layer&                            layer,
            erhe::physics::IWorld&                         physics_world,
            std::shared_ptr<erhe::scene::Mesh>             mesh,
            const std::vector<erhe::primitive::Primitive>& primitives
        )
            : build_info_set{build_info_set}
            , scene         {scene            }
            , layer         {layer            }
            , physics_world {physics_world    }
            , mesh          {mesh             }
            , primitives    {primitives       }
        {
        }

        erhe::primitive::Build_info_set&        build_info_set;
        erhe::scene::Scene&                     scene;
        erhe::scene::Layer&                     layer;
        erhe::physics::IWorld&                  physics_world;
        std::shared_ptr<erhe::scene::Mesh>      mesh;
        std::vector<erhe::primitive::Primitive> primitives;
    };
    Context                                              m_context;
    std::vector<Source_entry>                            m_source_entries;
    std::shared_ptr<erhe::primitive::Primitive_geometry> m_combined_primitive_geometry;
    std::vector<std::shared_ptr<erhe::scene::Node>>      m_selection_before;
    std::vector<std::shared_ptr<erhe::scene::Node>>      m_selection_after;
};

}
