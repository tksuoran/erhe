#include "operations/operation_stack.hpp"

#include "app_context.hpp"
#include "operations/operation.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

#include <taskflow/taskflow.hpp>

namespace editor {

Operation::~Operation() noexcept
{
}

#pragma region Commands
Undo_command::Undo_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "undo"}
    , m_context{context}
{
}

auto Undo_command::try_call() -> bool
{
    if (m_context.operation_stack->can_undo()) {
        m_context.operation_stack->undo();
        return true;
    } else {
        return false;
    }
}

Redo_command::Redo_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "redo"}
    , m_context{context}
{
}

auto Redo_command::try_call() -> bool
{
    if (m_context.operation_stack->can_redo()) {
        m_context.operation_stack->redo();
        return true;
    } else {
        return false;
    }
}
#pragma endregion Commands

Operation_stack::Operation_stack(
    tf::Executor&                executor,
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Operation Stack", "operation_stack", true}
    , m_context     {context}
    , m_executor    {executor}
    , m_undo_command{commands, context}
    , m_redo_command{commands, context}
{
    commands.register_command(&m_undo_command);
    commands.register_command(&m_redo_command);
    commands.bind_command_to_key(&m_undo_command, erhe::window::Key_z, true, erhe::window::Key_modifier_bit_ctrl);
    commands.bind_command_to_key(&m_redo_command, erhe::window::Key_y, true, erhe::window::Key_modifier_bit_ctrl);
    commands.bind_command_to_menu(&m_undo_command, "Edit.Undo");
    commands.bind_command_to_menu(&m_redo_command, "Edit.Redo");

    m_undo_command.set_host(this);
    m_redo_command.set_host(this);
}

Operation_stack::~Operation_stack() noexcept = default;

auto Operation_stack::get_executor() -> tf::Executor&
{
    return m_executor;
}

void Operation_stack::queue(const std::shared_ptr<Operation>& operation)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    m_queued.push_back(operation);
}

void Operation_stack::update()
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    if (m_queued.empty()) {
        return;
    }

    for (const auto& operation : m_queued) {
        operation->execute(m_context);
        m_executed.push_back(operation);
    }
    m_queued.clear();
    m_undone.clear();
}

void Operation_stack::undo()
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    if (m_executed.empty()) {
        return;
    }
    auto operation = m_executed.back(); // intentionally not a reference, otherwise pop_back() below will invalidate
    m_executed.pop_back();
    operation->undo(m_context);
    m_undone.push_back(operation);
}

void Operation_stack::redo()
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    if (m_undone.empty()) {
        return;
    }
    auto operation = m_undone.back(); // intentionally not a reference, otherwise pop_back() below will invalidate
    m_undone.pop_back();
    operation->execute(m_context);
    m_executed.push_back(operation);
}

auto Operation_stack::can_undo() const -> bool
{
    // TODO ? std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    return !m_executed.empty();
}

auto Operation_stack::can_redo() const -> bool
{
    // TODO ? std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    return !m_undone.empty();
}

void Operation_stack::imgui(const char* stack_label, const std::vector<std::shared_ptr<Operation>>& operations)
{
    const ImGuiTreeNodeFlags parent_flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };
    const ImGuiTreeNodeFlags leaf_flags{
        ImGuiTreeNodeFlags_SpanFullWidth    |
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_Leaf
    };

    if (ImGui::TreeNodeEx(stack_label, parent_flags)) {
        for (const auto& op : operations) {
            ImGui::TreeNodeEx(op->describe().c_str(), leaf_flags);
        }
        ImGui::TreePop();
    }
}

void Operation_stack::imgui()
{
    ERHE_PROFILE_FUNCTION();

    imgui("Executed", m_executed);
    imgui("Undone",   m_undone);
}

}
