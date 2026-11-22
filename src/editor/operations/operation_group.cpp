#include "operations/operation_group.hpp"

#include "operations/compound_operation.hpp"
#include "operations/operation.hpp"

#include "erhe_verify/verify.hpp"

namespace editor {

void Operation_group::open()
{
    ERHE_VERIFY(!m_open);
    m_open = true;
    m_operations.clear();
}

auto Operation_group::is_open() const -> bool
{
    return m_open;
}

auto Operation_group::get_size() const -> std::size_t
{
    return m_operations.size();
}

void Operation_group::add(const std::shared_ptr<Operation>& operation)
{
    ERHE_VERIFY(m_open);
    m_operations.push_back(operation);
}

auto Operation_group::close() -> std::shared_ptr<Operation>
{
    ERHE_VERIFY(m_open);
    m_open = false;
    std::shared_ptr<Operation> entry;
    if (m_operations.size() == 1) {
        entry = std::move(m_operations.front());
    } else if (m_operations.size() > 1) {
        // The operations have executed; the compound's execute() runs again only on redo.
        entry = std::make_shared<Compound_operation>(Compound_operation::Parameters{std::move(m_operations)});
    }
    m_operations.clear();
    return entry;
}

void Operation_group::undo_after(App_context& context, const std::size_t count)
{
    ERHE_VERIFY(m_open);
    ERHE_VERIFY(count <= m_operations.size());
    while (m_operations.size() > count) {
        const std::shared_ptr<Operation> operation = m_operations.back();
        m_operations.pop_back();
        operation->undo(context);
    }
}

void Operation_group::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    for (const std::shared_ptr<Operation>& operation : m_operations) {
        operation->collect_item_references(out_items);
    }
}

} // namespace editor
