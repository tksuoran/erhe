#pragma once

#include "operations/ioperation.hpp"

#include "erhe/scene/node.hpp"

#include <vector>

namespace editor
{

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

class Attach_operation
    : public IOperation
{
public:
    Attach_operation();
    Attach_operation(
        const std::shared_ptr<erhe::scene::Node_attachment>& attachment,
        const std::shared_ptr<erhe::scene::Node>&            host_node
    );

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

private:
    std::shared_ptr<erhe::scene::Node_attachment> m_attachment;
    std::shared_ptr<erhe::scene::Node>            m_host_node_before;
    std::shared_ptr<erhe::scene::Node>            m_host_node_after;
};

class Node_attach_operation
    : public IOperation
{
public:
    Node_attach_operation();
    Node_attach_operation(
        const std::shared_ptr<erhe::scene::Node>& parent,
        const std::shared_ptr<erhe::scene::Node>& child,
        const std::shared_ptr<erhe::scene::Node>  place_before_node,
        const std::shared_ptr<erhe::scene::Node>  place_after_node
    );

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

private:
    std::shared_ptr<erhe::scene::Node> m_child_node;
    std::shared_ptr<erhe::scene::Node> m_parent_before;
    std::size_t                        m_parent_before_index;
    std::shared_ptr<erhe::scene::Node> m_parent_after;
    std::size_t                        m_parent_after_index;
    std::shared_ptr<erhe::scene::Node> m_place_before_node;
    std::shared_ptr<erhe::scene::Node> m_place_after_node;
};

class Node_reposition_in_parent_operation
    : public IOperation
{
public:
    Node_reposition_in_parent_operation();
    Node_reposition_in_parent_operation(
        const std::shared_ptr<erhe::scene::Node>& child_node,
        const std::shared_ptr<erhe::scene::Node>  place_before_node,
        const std::shared_ptr<erhe::scene::Node>  palce_after_node
    );

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

private:
    std::shared_ptr<erhe::scene::Node> m_child_node;
    std::size_t                        m_before_index;
    std::shared_ptr<erhe::scene::Node> m_place_before_node;
    std::shared_ptr<erhe::scene::Node> m_place_after_node;
};

}
