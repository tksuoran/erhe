#pragma once

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_profile/profile.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace erhe::commands { class Commands; }
namespace erhe::imgui    { class Imgui_windows; }

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

// Main-thread-only: every entry point verifies the caller is on the main
// thread (App_context::main_thread_id) instead of locking. All operation
// sources (ImGui windows/tools, commands, MCP handlers dispatched via
// Mcp_server::process_queued_requests(), startup script, message-bus
// callbacks) run on the main thread; the single exception for worker
// threads is queue_from_thread(), a mutex-protected inbox drained by
// update() - used by the async mesh-operation completions.
//
// Re-entrancy contract: while an operation is executing (or being undone),
// the only legal Operation_stack call is queue(). Operations queued during
// execution run later in the same update() pass, in append order.
class Operation_stack
    : public erhe::imgui::Imgui_window
    , public erhe::commands::Command_host
{
public:
    Operation_stack(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context
    );
    ~Operation_stack() noexcept override;

    [[nodiscard]] auto can_undo      () const -> bool;
    [[nodiscard]] auto can_redo      () const -> bool;
    void queue(const std::shared_ptr<Operation>& operation);

    // Thread-safe variant of queue() for async worker completions (the
    // async_for_nodes_with_mesh callbacks run on tf::Executor workers):
    // appends to a mutex-protected inbox that update() drains on the main
    // thread before executing. This is the ONLY Operation_stack entry point
    // legal off the main thread.
    void queue_from_thread(const std::shared_ptr<Operation>& operation);

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

private:
    friend class Mcp_server;

    [[nodiscard]] auto get_undo_stack() const -> const std::vector<std::shared_ptr<Operation>>&;
    [[nodiscard]] auto get_redo_stack() const -> const std::vector<std::shared_ptr<Operation>>&;

    void imgui(const char* stack_label, const std::vector<std::shared_ptr<Operation>>& operations);

    void verify_main_thread() const;

    App_context&  m_context;
    Undo_command  m_undo_command;
    Redo_command  m_redo_command;

    bool                                    m_executing{false};
    std::vector<std::shared_ptr<Operation>> m_executed;
    std::vector<std::shared_ptr<Operation>> m_undone;
    std::vector<std::shared_ptr<Operation>> m_queued;

    // Cross-thread inbox for queue_from_thread(); drained by update().
    ERHE_PROFILE_MUTEX(std::mutex,          m_thread_queue_mutex);
    std::vector<std::shared_ptr<Operation>> m_queued_from_threads;
};

}
