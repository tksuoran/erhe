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

class Scene_manager;
class Selection_tool;

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
        std::shared_ptr<erhe::scene::Mesh> mesh;
        erhe::scene::Mesh before;
        erhe::scene::Mesh after;
    };

    void make_entries(Context& context,
                      std::function<erhe::geometry::Geometry(erhe::geometry::Geometry&)> operation);

    // Implements IOperation
    void execute() override;
    void undo() override;

    void add_entry(Entry&& entry);

private:
    std::vector<Entry>              m_entries;
    std::shared_ptr<Selection_tool> m_selection_tool;
};

}
