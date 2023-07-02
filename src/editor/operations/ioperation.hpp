#pragma once

#include <string>

namespace editor
{

class Editor_context;

class IOperation
{
public:
    virtual ~IOperation() noexcept;

    virtual void execute (Editor_context& context) = 0;
    virtual void undo    (Editor_context& context) = 0;
    virtual auto describe() const -> std::string = 0;
};

} // namespace editor
