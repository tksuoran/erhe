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
    class IWorld;
}

namespace erhe::primitive
{
    class Build_info_set;
}

namespace erhe::scene
{
    class Mesh;
    class Mesh_layer;
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
        erhe::primitive::Build_info_set& build_info_set;
        erhe::scene::Scene&              scene;
        erhe::scene::Mesh_layer&         layer;
        erhe::physics::IWorld&           physics_world;
        std::shared_ptr<Selection_tool>  selection_tool;
    };

protected:
    class Entry
    {
    public:
        erhe::scene::Scene&                scene;
        erhe::scene::Mesh_layer&           layer;
        erhe::physics::IWorld&             physics_world;
        std::shared_ptr<erhe::scene::Mesh> mesh;
        erhe::scene::Mesh_data             before;
        erhe::scene::Mesh_data             after;
    };

    Mesh_operation ();
    ~Mesh_operation() override;

    void make_entries(
        const Context&                                                           context,
        const std::function<erhe::geometry::Geometry(erhe::geometry::Geometry&)> operation
    );

    // Implements IOperation
    void execute() override;
    void undo   () override;

    void add_entry(Entry&& entry);

private:
    std::vector<Entry>              m_entries;
    std::shared_ptr<Selection_tool> m_selection_tool;
};

}
