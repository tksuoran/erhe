#pragma once

#include <cstddef>
#include <memory>
#include <unordered_set>
#include <vector>

namespace erhe { class Item_base; }

namespace editor {

class App_context;
class Operation;

// The operations of an open undo group (Operation_stack::begin_group()), already executed and not yet recorded.
class Operation_group
{
public:
    void open();
    [[nodiscard]] auto is_open () const -> bool;
    [[nodiscard]] auto get_size() const -> std::size_t;
    void add(const std::shared_ptr<Operation>& operation);

    // Closes the group; returns the one entry to record (a Compound_operation for more than one), null when empty.
    [[nodiscard]] auto close() -> std::shared_ptr<Operation>;

    // Undoes the operations after the first count in reverse order and drops them; the group stays open.
    void undo_after(App_context& context, std::size_t count);
    // The items the held operations retain (Operation::collect_item_references()).
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const;

private:
    bool                                    m_open{false};
    std::vector<std::shared_ptr<Operation>> m_operations;
};

} // namespace editor
