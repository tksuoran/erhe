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

// NOTE This is not currently used.
// TODO Item_operation
//class Node_operation
//    : public IOperation
//{
//protected:
//    class Entry
//    {
//    public:
//        std::shared_ptr<erhe::scene::Node> node;
//        erhe::scene::Node_data             before;
//        erhe::scene::Node_data             after;
//    };
//
//    // Implements IOperation
//    [[nodiscard]] auto describe() const -> std::string override;
//    void execute(Editor_context& context) override;
//    void undo   (Editor_context& context) override;
//
//    // Public API
//    void add_entry(Entry&& entry);
//
//private:
//    std::vector<Entry> m_entries;
//};

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

}
