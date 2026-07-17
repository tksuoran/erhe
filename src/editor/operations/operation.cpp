#include "operations/operation.hpp"

namespace editor {

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

}
