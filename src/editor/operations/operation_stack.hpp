#pragma once

#include "erhe/commands/command.hpp"
#include "erhe/imgui/imgui_window.hpp"

#include <memory>
#include <vector>

namespace erhe::commands {
    class CommandS;
}
namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor
{

class Editor_context;
class Editor_message_bus;
class IOperation;
class Editor_context;
class Operation_stack;
class Selection_tool;

class Undo_command
    : public erhe::commands::Command
{
public:
    Undo_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};

class Redo_command
    : public erhe::commands::Command
{
public:
    Redo_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};

class Operation_stack
    : public erhe::imgui::Imgui_window
    , public erhe::commands::Command_host
{
public:
    Operation_stack(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

    [[nodiscard]] auto can_undo() const -> bool;
    [[nodiscard]] auto can_redo() const -> bool;
    void push(const std::shared_ptr<IOperation>& operation);
    void undo();
    void redo();

    // Implements Window
    void imgui() override;

private:
    void imgui(
        const char*                                     stack_label,
        const std::vector<std::shared_ptr<IOperation>>& operations
    );

    Editor_context& m_context;

    Undo_command m_undo_command;
    Redo_command m_redo_command;

    std::vector<std::shared_ptr<IOperation>> m_executed;
    std::vector<std::shared_ptr<IOperation>> m_undone;
};

} // namespace editor
