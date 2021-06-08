#include "operations/operation_stack.hpp"
#include "operations/ioperation.hpp"

namespace editor
{

Operation_stack::Operation_stack()
    : erhe::components::Component{c_name}
{
}

Operation_stack::~Operation_stack() = default;

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
    operation->execute();
    m_executed.push(operation);
}


}
