#pragma once

#include <memory>
#include <string>

namespace erhe::components
{
    class Components;
}

namespace editor
{

class IOperation
{
public:
    virtual ~IOperation() noexcept;

    virtual void execute () = 0;
    virtual void undo    () = 0;
    virtual auto describe() const -> std::string = 0;
};

} // namespace editor
