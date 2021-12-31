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
        Selection_tool*                  selection_tool{nullptr};
    };

protected:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        erhe::scene::Mesh_data             before;
        erhe::scene::Mesh_data             after;
    };

    explicit Mesh_operation(Context&& context);
    ~Mesh_operation() override;

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute () const override;
    void undo    () const override;

    // Public API
    void add_entry(Entry&& entry);
    void make_entries(
        const std::function<erhe::geometry::Geometry(erhe::geometry::Geometry&)> operation
    );

private:
    Context            m_context;
    std::vector<Entry> m_entries;
    Selection_tool*    m_selection_tool{nullptr};
};

}
