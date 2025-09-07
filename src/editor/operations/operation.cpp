#include "operations/operation.hpp"

namespace editor {

void Operation::set_description(std::string&& description)
{
    m_description = std::move(description);
}

auto Operation::describe() const -> const std::string&
{
    return m_description;
}

}
