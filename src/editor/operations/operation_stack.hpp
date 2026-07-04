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
class Mcp_server;
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
    ~Operation_stack() noexcept override;

    [[nodiscard]] auto can_undo      () const -> bool;
    [[nodiscard]] auto can_redo      () const -> bool;
    void queue(const std::shared_ptr<Operation>& operation);

    // Executes the operation immediately (caller must be on the main
    // thread) and records it for undo. Used where the caller needs the
    // operation's effects to be observable right away, e.g. the MCP
    // server responding to a tool call, or geometry graph edits whose
    // evaluation runs in the same frame.
    void execute_now(const std::shared_ptr<Operation>& operation);

    void undo();
    void redo();

    // Drops the undo and redo histories (queued operations are kept). Used
    // when a scene is closed: recorded operations hold shared_ptrs to scene
    // content and viewport resources, which would keep a closed scene's
    // objects alive -- and its viewport rendergraph nodes registered and
    // executing -- indefinitely.
    void clear_history();

    void update();

    // Implements Window
    void imgui() override;

    [[nodiscard]] auto get_executor() -> tf::Executor&;

private:
    friend class Mcp_server;

    [[nodiscard]] auto get_undo_stack() const -> const std::vector<std::shared_ptr<Operation>>&;
    [[nodiscard]] auto get_redo_stack() const -> const std::vector<std::shared_ptr<Operation>>&;

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
