#pragma once

#include "operations/ioperation.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/transform.hpp"

#include <memory>
#include <string>
#include <vector>

namespace erhe::physics
{
    class IWorld;
};

namespace erhe::primitive
{
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
    class Parameters
    {
    public:
        erhe::scene::Scene&      scene;
        erhe::scene::Mesh_layer& layer;
        bool                     attach{true};

        Selection_tool* selection_tool{nullptr};
    };

    explicit Attach_detach_operation(Parameters&& context);

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

private:
    void execute(bool attach) const;

    struct Entry
    {
        explicit Entry(const std::shared_ptr<erhe::scene::Node>& node)
            : node            {node}
            , before_transform{node->parent_from_node_transform()}
        {
        }

        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Transform             before_transform;
    };

    Parameters                         m_parameters;
    std::shared_ptr<erhe::scene::Node> m_parent_node;
    std::vector<Entry>                 m_entries;
};

}
