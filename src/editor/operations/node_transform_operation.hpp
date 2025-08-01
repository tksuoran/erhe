#pragma once

#include "operations/ioperation.hpp"

#include "erhe_scene/node.hpp"

#include <memory>

namespace erhe        { class Hierarchy; }
namespace erhe::scene { class Node; }

namespace editor {

class Node_transform_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Transform             parent_from_node_before;
        erhe::scene::Transform             parent_from_node_after;
        float                              time_duration{0.0f};
    };

    explicit Node_transform_operation(const Parameters& parameters);

    // Implements Operation
    auto describe() const -> std::string override;
    void execute (App_context& context)  override;
    void undo    (App_context& context)  override;

private:
    Parameters m_parameters;
};

}
