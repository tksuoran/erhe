#pragma once

#include "operations/ioperation.hpp"

#include "erhe_scene/node.hpp"

#include <memory>
#include <vector>

namespace erhe {
    class Hierarchy;
}

namespace erhe::scene {
    class Node;
}

namespace editor
{

// NOTE This is not currently unused.
// TODO Item_operation
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
    void execute(Editor_context& context) override;
    void undo   (Editor_context& context) override;

    // Public API
    void add_entry(Entry&& entry);

private:
    std::vector<Entry> m_entries;
};

class Node_transform_operation
    : public IOperation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Transform             parent_from_node_before;
        erhe::scene::Transform             parent_from_node_after;
    };

    explicit Node_transform_operation(const Parameters& parameters);

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(Editor_context& context) override;
    void undo   (Editor_context& context) override;

private:
    Parameters m_parameters;
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
    void execute(Editor_context& context) override;
    void undo   (Editor_context& context) override;

private:
    std::shared_ptr<erhe::scene::Node_attachment> m_attachment;
    std::shared_ptr<erhe::scene::Node>            m_host_node_before;
    std::shared_ptr<erhe::scene::Node>            m_host_node_after;
};

class Item_parent_change_operation
    : public IOperation
{
public:
    Item_parent_change_operation();
    Item_parent_change_operation(
        const std::shared_ptr<erhe::Hierarchy>& parent,
        const std::shared_ptr<erhe::Hierarchy>& child,
        const std::shared_ptr<erhe::Hierarchy>  place_before,
        const std::shared_ptr<erhe::Hierarchy>  place_after
    );

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(Editor_context& context) override;
    void undo   (Editor_context& context) override;

private:
    std::shared_ptr<erhe::Hierarchy> m_child              {};
    std::shared_ptr<erhe::Hierarchy> m_parent_before      {};
    std::size_t                      m_parent_before_index{0};
    std::shared_ptr<erhe::Hierarchy> m_parent_after       {};
    std::size_t                      m_parent_after_index {0};
    std::shared_ptr<erhe::Hierarchy> m_place_before       {};
    std::shared_ptr<erhe::Hierarchy> m_place_after        {};
};

class Item_reposition_in_parent_operation
    : public IOperation
{
public:
    Item_reposition_in_parent_operation();
    Item_reposition_in_parent_operation(
        const std::shared_ptr<erhe::Hierarchy>& child,
        const std::shared_ptr<erhe::Hierarchy>  place_before,
        const std::shared_ptr<erhe::Hierarchy>  palce_after
    );

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(Editor_context& context) override;
    void undo   (Editor_context& context) override;

private:
    std::shared_ptr<erhe::Hierarchy> m_child;
    std::size_t                      m_before_index{0};
    std::shared_ptr<erhe::Hierarchy> m_place_before{};
    std::shared_ptr<erhe::Hierarchy> m_place_after {};
};

}
