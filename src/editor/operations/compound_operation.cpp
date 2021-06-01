#include "operations/compound_operation.hpp"

namespace editor
{

Compound_operation::Compound_operation(const Context& context)
    : m_context{context}
{
}

Compound_operation::~Compound_operation() = default;

void Compound_operation::execute()
{
    for (auto operation : m_context.operations)
    {
        operation->execute();
    }
}

void Compound_operation::undo()
{
    for (auto i = rbegin(m_context.operations),
         end = rend(m_context.operations);
         i < end;
         ++i)
    {
        auto operation = *i;
        operation->undo();
    }
}


}