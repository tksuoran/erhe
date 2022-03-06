#pragma once

#include "operations/ioperation.hpp"

#include "erhe/scene/node.hpp"

#include <vector>

namespace erhe::scene
{
    class Scene;
}

namespace editor
{

class Selection_tool;

class Node_operation
    : public IOperation
{
protected:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Node_data             before;
        erhe::scene::Node_data             after;
    };

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

    // Public API
    void add_entry(Entry&& entry);

private:
    std::vector<Entry> m_entries;
};

class Node_attach_operation
    : public IOperation
{
public:
    Node_attach_operation();
    Node_attach_operation(
        const std::shared_ptr<erhe::scene::Node>& parent,
        const std::shared_ptr<erhe::scene::Node>& child
    );
    Node_attach_operation(const Node_attach_operation&);
    Node_attach_operation(Node_attach_operation&& other);
    auto operator=(const Node_attach_operation&) -> Node_attach_operation&;
    auto operator=(Node_attach_operation&& other) -> Node_attach_operation&;

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

private:
    std::shared_ptr<erhe::scene::Node> m_child_node;
    std::shared_ptr<erhe::scene::Node> m_parent_before;
    size_t                             m_parent_before_index;
    std::shared_ptr<erhe::scene::Node> m_parent_after;
    size_t                             m_parent_after_index;
};

}
