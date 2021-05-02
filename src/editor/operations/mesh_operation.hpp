#pragma once

#include "operations/ioperation.hpp"
#include "erhe/scene/mesh.hpp"
#include <functional>
#include <vector>

namespace erhe::geometry
{
    class Geometry;
}

namespace editor
{

class Mesh_operation
    : public IOperation
{
public:
    struct Context
    {
        std::shared_ptr<Scene_manager>  scene_manager;
        std::shared_ptr<Selection_tool> selection_tool;
    };

protected:
    struct Entry
    {
        erhe::scene::Mesh* mesh;
        erhe::scene::Mesh  before;
        erhe::scene::Mesh  after;
    };

    void make_entries(Context& context,
                      std::function<erhe::geometry::Geometry(erhe::geometry::Geometry&)> operation);
    void execute() override;
    void undo() override;

    void add_entry(Entry&& entry);

private:
    std::vector<Entry> m_entries;
};

}
