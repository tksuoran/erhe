#pragma once

#include "erhe/components/component.hpp"

#include <memory>
#include <stack>

namespace editor
{

class IOperation;

class Operation_stack
    : public erhe::components::Component
{
public:
    static constexpr const char* c_name = "Operation_stack";
    Operation_stack() : erhe::components::Component{c_name} {}
    virtual ~Operation_stack() = default;

    void push(std::shared_ptr<IOperation> operation);
    void undo();
    void redo();
    auto can_undo() const -> bool
    {
        return !m_executed.empty();
    }
    auto can_redo() const -> bool
    {
        return !m_undone.empty();
    }

private:
    std::stack<std::shared_ptr<IOperation>> m_executed;
    std::stack<std::shared_ptr<IOperation>> m_undone;
};

}
