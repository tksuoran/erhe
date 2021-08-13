#pragma once

#include "operations/ioperation.hpp"
#include "erhe/scene/mesh.hpp"

#include <functional>
#include <vector>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::physics
{
    class World;
}

namespace erhe::primitive
{
    class Geometry_uploader;
}

namespace erhe::scene
{
    class Layer;
    class Mesh;
    class Node;
    class Scene;
}

namespace editor
{

class Scene_manager;
class Selection_tool;

class Mesh_operation
    : public IOperation
{
public:
    class Context
    {
    public:
        erhe::primitive::Geometry_uploader& geometry_uploader;
        erhe::scene::Layer&                 layer;
        erhe::scene::Scene&                 scene;
        erhe::physics::World&               physics_world;
        std::shared_ptr<Selection_tool>     selection_tool;
    };

protected:
    class Entry
    {
    public:
        erhe::scene::Layer&                layer;
        erhe::scene::Scene&                scene;
        erhe::physics::World&              physics_world;
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Mesh                  before;
        erhe::scene::Mesh                  after;
    };

    Mesh_operation ();
    ~Mesh_operation() override;

    void make_entries(const Context& context,
                      const std::function<erhe::geometry::Geometry(erhe::geometry::Geometry&)> operation);

    // Implements IOperation
    void execute() override;
    void undo   () override;

    void add_entry(Entry&& entry);

private:
    std::vector<Entry>              m_entries;
    std::shared_ptr<Selection_tool> m_selection_tool;
};

}
