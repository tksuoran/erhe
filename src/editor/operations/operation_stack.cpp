#include "operations/operation_stack.hpp"
#include "operations/ioperation.hpp"

namespace sample
{

void Operation_stack::push(std::shared_ptr<IOperation> operation)
{
    operation->execute();
    m_executed.push(operation);
}

void Operation_stack::undo()
{
    if (m_executed.empty())
    {
        return;
    }
    auto operation = m_executed.top();
    m_executed.pop();
    operation->undo();
    m_undone.push(operation);
}

void Operation_stack::redo()
{
    if (m_undone.empty())
    {
        return;
    }
    auto operation = m_undone.top();
    m_undone.pop();
    operation->undo();
    m_executed.push(operation);
}


}
