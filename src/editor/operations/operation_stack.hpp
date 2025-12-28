#pragma once

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_profile/profile.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace erhe::commands { class Commands; }
namespace erhe::imgui    { class Imgui_windows; }
namespace tf             { class Executor; }

namespace editor {

class App_context;
class App_message_bus;
class Operation;
class App_context;
class Operation_stack;
class Selection_tool;

class Undo_command : public erhe::commands::Command
{
public:
    Undo_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Redo_command : public erhe::commands::Command
{
public:
    Redo_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Operation_stack
    : public erhe::imgui::Imgui_window
    , public erhe::commands::Command_host
{
public:
    Operation_stack(
        tf::Executor&                executor,
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context
    );
    ~Operation_stack() noexcept;

    [[nodiscard]] auto can_undo() const -> bool;
    [[nodiscard]] auto can_redo() const -> bool;
    void queue(const std::shared_ptr<Operation>& operation);
    void undo();
    void redo();

    void update();

    // Implements Window
    void imgui() override;

    [[nodiscard]] auto get_executor() -> tf::Executor&;

private:
    void imgui(const char* stack_label, const std::vector<std::shared_ptr<Operation>>& operations);

    App_context&  m_context;
    tf::Executor& m_executor;
    Undo_command  m_undo_command;
    Redo_command  m_redo_command;

    ERHE_PROFILE_MUTEX(std::mutex,          m_mutex);
    std::vector<std::shared_ptr<Operation>> m_executed;
    std::vector<std::shared_ptr<Operation>> m_undone;
    std::vector<std::shared_ptr<Operation>> m_queued;
};

}
