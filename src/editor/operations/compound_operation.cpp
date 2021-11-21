#include "operations/compound_operation.hpp"

#include <sstream>

namespace editor
{

Compound_operation::Compound_operation(const Context& context)
    : m_context{context}
{
}

Compound_operation::~Compound_operation() = default;

void Compound_operation::execute() const
{
    for (auto operation : m_context.operations)
    {
        operation->execute();
    }
}

void Compound_operation::undo() const
{
    for (
        auto i = rbegin(m_context.operations),
        end = rend(m_context.operations);
        i < end;
        ++i)
    {
        auto operation = *i;
        operation->undo();
    }
}

auto Compound_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Compound ";
    bool first = true;
    for (auto operation : m_context.operations)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            ss << ", ";
        }
        ss << operation->describe();
    }
    return ss.str();
}


}