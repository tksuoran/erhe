#pragma once

#include <memory>
#include <string>

namespace editor
{

class IOperation
{
public:
    virtual ~IOperation() {}
    virtual void execute () const = 0;
    virtual void undo    () const = 0;
    virtual auto describe() const -> std::string = 0;
};

} // namespace editor
