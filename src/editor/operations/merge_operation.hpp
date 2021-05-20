#pragma once

#include "operations/ioperation.hpp"
#include "erhe/scene/mesh.hpp"
#include <vector>

namespace editor
{

class Scene_manager;
class Selection_tool;

class Merge_operation
    : public IOperation
{
public:
    struct Context
    {
        std::shared_ptr<Scene_manager>  scene_manager;
        std::shared_ptr<Selection_tool> selection_tool;
    };

    explicit Merge_operation(Context& context);

    // Implements IOperation
    void execute() override;
    void undo() override;

private:
    struct Source_entry
    {
        std::shared_ptr<erhe::scene::Mesh>      mesh;
        std::vector<erhe::primitive::Primitive> primitives;
    };
    Context                                              m_context;
    std::vector<Source_entry>                            m_source_entries;
    std::shared_ptr<erhe::primitive::Primitive_geometry> m_combined_primitive_geometry;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>      m_selection_before;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>      m_selection_after;
};

}
