#pragma once

#include "operations/ioperation.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include <vector>

namespace erhe::physics
{
    class World;
};

namespace erhe::primitive
{
    struct Primitive_build_context;
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
    struct Context
    {
        erhe::primitive::Primitive_build_context& primitive_build_context;
        erhe::scene::Layer&                       layer;
        erhe::scene::Scene&                       scene;
        erhe::physics::World&                     physics_world;
        std::shared_ptr<Selection_tool>           selection_tool;
    };

    explicit Merge_operation(Context& context);

    // Implements IOperation
    void execute() override;
    void undo() override;

private:
    struct Source_entry
    {
        erhe::primitive::Primitive_build_context& primitive_build_context;
        erhe::scene::Layer&                       layer;
        erhe::scene::Scene&                       scene;
        erhe::physics::World&                     physics_world;
        std::shared_ptr<erhe::scene::Mesh>        mesh;
        std::shared_ptr<erhe::scene::Node>        node;
        std::vector<erhe::primitive::Primitive>   primitives;
    };
    Context                                                     m_context;
    std::vector<Source_entry>                                   m_source_entries;
    std::shared_ptr<erhe::primitive::Primitive_geometry>        m_combined_primitive_geometry;
    std::vector<std::shared_ptr<erhe::scene::INode_attachment>> m_selection_before;
    std::vector<std::shared_ptr<erhe::scene::INode_attachment>> m_selection_after;
};

}
