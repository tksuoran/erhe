#pragma once

#include "operations/operation.hpp"

#include <memory>

namespace erhe {
    class Hierarchy;
}
namespace erhe::scene {
    class Node;
    class Node_attachment;
}

namespace editor {

class Node_attach_operation : public Operation
{
public:
    Node_attach_operation();
    Node_attach_operation(const std::shared_ptr<erhe::scene::Node_attachment>& attachment, const std::shared_ptr<erhe::scene::Node>& host_node);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    std::shared_ptr<erhe::scene::Node_attachment> m_attachment;
    std::shared_ptr<erhe::scene::Node>            m_host_node_before;
    std::shared_ptr<erhe::scene::Node>            m_host_node_after;
};

}
