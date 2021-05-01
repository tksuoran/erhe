#pragma once

#include <memory>

namespace sample
{

class Scene_manager;
class Selection_tool;

class IOperation
{
public:
    virtual void execute() = 0;
    virtual void undo() = 0;
};

} // namespace sample
