#include "operations/compound_operation.hpp"

#include <sstream>

namespace editor
{

Compound_operation::Parameters::Parameters()
{
}

Compound_operation::Parameters::~Parameters()
{
}

Compound_operation::Compound_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
}

Compound_operation::~Compound_operation()
{
}

void Compound_operation::execute(const Operation_context& context)
{
    for (auto& operation : m_parameters.operations)
    {
        operation->execute(context);
    }
}

void Compound_operation::undo(const Operation_context& context)
{
    for (
        auto i = rbegin(m_parameters.operations),
        end = rend(m_parameters.operations);
        i < end;
        ++i
    )
    {
        auto& operation = *i;
        operation->undo(context);
    }
}

auto Compound_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Compound ";
    bool first = true;
    for (auto& operation : m_parameters.operations)
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