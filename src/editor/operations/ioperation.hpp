#pragma once

#include <memory>
#include <string>

namespace erhe::components
{
    class Components;
}

namespace editor
{

class Operation_context
{
public:
    const erhe::components::Components* components;
};

class IOperation
{
public:
    virtual ~IOperation() noexcept;

    virtual void execute (const Operation_context& context) = 0;
    virtual void undo    (const Operation_context& context) = 0;
    virtual auto describe() const -> std::string = 0;
};

} // namespace editor
