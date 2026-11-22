#include "operations/operation.hpp"

namespace editor {

Operation::~Operation() noexcept
{
}

void Operation::on_lossless_undo(App_context&)
{
    // Default: an operation that cannot rebuild itself keeps what it holds.
}

auto Operation::has_droppable_payload() const -> bool
{
    return false;
}

void Operation::drop_payload()
{
}

void Operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    static_cast<void>(out_items);
}

void Operation::set_description(std::string&& description)
{
    m_description = std::move(description);
}

auto Operation::describe() const -> const std::string&
{
    return m_description;
}

void Operation::set_error(std::string error)
{
    m_error = std::move(error);
}

auto Operation::get_error() const -> const std::string&
{
    return m_error;
}

auto Operation::has_error() const -> bool
{
    return !m_error.empty();
}

auto Operation::is_refused() const -> bool
{
    return m_refused;
}

void Operation::set_refused(std::string reason)
{
    m_error   = std::move(reason);
    m_refused = true;
}

auto execute_unless_refused(App_context& context, const std::span<const std::shared_ptr<Operation>> operations) -> std::optional<std::string>
{
    for (std::size_t i = 0, end = operations.size(); i < end; ++i) {
        operations[i]->execute(context);
        if (operations[i]->is_refused()) {
            for (std::size_t j = i; j > 0; --j) {
                operations[j - 1]->undo(context);
            }
            return operations[i]->get_error();
        }
    }
    return std::nullopt;
}

}
