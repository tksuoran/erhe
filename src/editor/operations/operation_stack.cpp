#include "operations/operation_stack.hpp"

#include "app_context.hpp"
#include "operations/operation.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

#include <thread>

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
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Operation Stack", "operation_stack", true}
    , m_context     {context}
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

void Operation_stack::verify_main_thread() const
{
    ERHE_VERIFY(std::this_thread::get_id() == m_context.main_thread_id);
}

void Operation_stack::queue(const std::shared_ptr<Operation>& operation)
{
    verify_main_thread();

    // Legal also while an operation is executing (m_executing); the queued
    // operation runs later in the same update() pass.
    m_queued.push_back(operation);
}

void Operation_stack::queue_from_thread(const std::shared_ptr<Operation>& operation)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_thread_queue_mutex};
    m_queued_from_threads.push_back(operation);
}

void Operation_stack::execute_now(const std::shared_ptr<Operation>& operation)
{
    verify_main_thread();
    ERHE_VERIFY(!m_executing);

    m_executing = true;
    operation->execute(m_context);
    m_executing = false;
    m_executed.push_back(operation);
    m_undone.clear();
}

void Operation_stack::update()
{
    ERHE_PROFILE_FUNCTION();

    verify_main_thread();
    ERHE_VERIFY(!m_executing);

    // Drain the cross-thread inbox (async worker completions) into the
    // main-thread queue, preserving arrival order.
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_thread_queue_mutex};
        if (!m_queued_from_threads.empty()) {
            m_queued.insert(
                m_queued.end(),
                std::make_move_iterator(m_queued_from_threads.begin()),
                std::make_move_iterator(m_queued_from_threads.end())
            );
            m_queued_from_threads.clear();
        }
    }

    if (m_queued.empty()) {
        return;
    }

    // Index loop with a per-iteration copy: an executing operation may
    // queue() follow-up operations, growing (and reallocating) m_queued.
    for (std::size_t i = 0; i < m_queued.size(); ++i) {
        std::shared_ptr<Operation> operation = m_queued[i];
        m_executing = true;
        operation->execute(m_context);
        m_executing = false;
        m_executed.push_back(std::move(operation));
    }
    m_queued.clear();
    m_undone.clear();
}

void Operation_stack::undo()
{
    verify_main_thread();
    ERHE_VERIFY(!m_executing);

    if (m_executed.empty()) {
        return;
    }
    auto operation = m_executed.back(); // intentionally not a reference, otherwise pop_back() below will invalidate
    m_executed.pop_back();
    m_executing = true;
    operation->undo(m_context);
    m_executing = false;
    m_undone.push_back(operation);
}

void Operation_stack::clear_history()
{
    verify_main_thread();
    ERHE_VERIFY(!m_executing);

    m_executed.clear();
    m_undone.clear();
}

void Operation_stack::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    verify_main_thread();
    for (const std::shared_ptr<Operation>& operation : m_executed) {
        operation->collect_item_references(out_items);
    }
    for (const std::shared_ptr<Operation>& operation : m_undone) {
        operation->collect_item_references(out_items);
    }
    for (const std::shared_ptr<Operation>& operation : m_queued) {
        operation->collect_item_references(out_items);
    }
}

void Operation_stack::redo()
{
    verify_main_thread();
    ERHE_VERIFY(!m_executing);

    if (m_undone.empty()) {
        return;
    }
    auto operation = m_undone.back(); // intentionally not a reference, otherwise pop_back() below will invalidate
    m_undone.pop_back();
    m_executing = true;
    operation->execute(m_context);
    m_executing = false;
    m_executed.push_back(operation);
}

auto Operation_stack::can_undo() const -> bool
{
    verify_main_thread();

    return !m_executed.empty();
}

auto Operation_stack::can_redo() const -> bool
{
    verify_main_thread();

    return !m_undone.empty();
}

auto Operation_stack::get_undo_stack() const -> const std::vector<std::shared_ptr<Operation>>&
{
    return m_executed;
}

auto Operation_stack::get_redo_stack() const -> const std::vector<std::shared_ptr<Operation>>&
{
    return m_undone;
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
