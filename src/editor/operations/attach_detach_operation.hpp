#pragma once

#include "operations/ioperation.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/transform.hpp"

#include <string>
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
    class Mesh_layer;
    class Scene;
};

namespace editor
{

class Scene_root;
class Selection_tool;

class Attach_detach_operation
    : public IOperation
{
public:
    class Context
    {
    public:
        erhe::scene::Scene&      scene;
        erhe::scene::Mesh_layer& layer;
        Selection_tool*          selection_tool;
        bool                     attach{true};
    };

    explicit Attach_detach_operation(Context& context);

    // Implements IOperation
    void execute () const override;
    void undo    () const override;
    auto describe() const -> std::string override;

private:
    void execute(bool attach) const;

    struct Entry
    {
        Entry(const std::shared_ptr<erhe::scene::Node>& node)
            : node            {node}
            , before_transform{node->parent_from_node_transform()}
        {
        }

        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Transform             before_transform;
    };

    Context                            m_context;
    std::shared_ptr<erhe::scene::Node> m_parent_node;
    std::vector<Entry>                 m_entries;
};

}
