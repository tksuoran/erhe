#pragma once

#include <memory>

namespace editor
{

class IOperation
{
public:
    virtual ~IOperation() {}
    virtual void execute() = 0;
    virtual void undo   () = 0;
};

} // namespace editor
