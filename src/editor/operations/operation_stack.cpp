#include "operations/operation_stack.hpp"
#include "operations/ioperation.hpp"
#include "tools/tool.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

class Undo_command
    : public erhe::application::Command
{
public:
    Undo_command();
    auto try_call() -> bool override;
};

class Redo_command
    : public erhe::application::Command
{
public:
    Redo_command();
    auto try_call() -> bool override;
};

IOperation::~IOperation() noexcept
{
}

#pragma region Commands
Undo_command::Undo_command()
    : Command{"undo"}
{
}

auto Undo_command::try_call() -> bool
{
    if (g_operation_stack->can_undo()) {
        g_operation_stack->undo();
        return true;
    } else {
        return false;
    }
}

Redo_command::Redo_command()
    : Command{"redo"}
{
}

auto Redo_command::try_call() -> bool
{
    if (g_operation_stack->can_redo()) {
        g_operation_stack->redo();
        return true;
    } else {
        return false;
    }
}
#pragma endregion Commands

IOperation_stack::~IOperation_stack() noexcept = default;

class Operation_stack_impl
    : IOperation_stack
    , public Tool
    , public erhe::application::Imgui_window
{
public:
    Operation_stack_impl()
        : erhe::application::Imgui_window{Operation_stack::c_title}
    {
        ERHE_VERIFY(g_operation_stack == nullptr);
        g_operation_stack = this;

        erhe::application::g_imgui_windows->register_imgui_window(this, "operation_stack");

        erhe::application::g_commands->register_command(&m_undo_command);
        erhe::application::g_commands->register_command(&m_redo_command);
        erhe::application::g_commands->bind_command_to_key(&m_undo_command, erhe::toolkit::Key_z, true, erhe::toolkit::Key_modifier_bit_ctrl);
        erhe::application::g_commands->bind_command_to_key(&m_redo_command, erhe::toolkit::Key_y, true, erhe::toolkit::Key_modifier_bit_ctrl);

        m_undo_command.set_host(this);
        m_redo_command.set_host(this);
    }

    ~Operation_stack_impl() noexcept override
    {
        ERHE_VERIFY(g_operation_stack == this);
        g_operation_stack = nullptr;
    }

    // Implements Window
    void imgui() override;

    [[nodiscard]] auto can_undo() const -> bool override;
    [[nodiscard]] auto can_redo() const -> bool override;
    void push(const std::shared_ptr<IOperation>& operation) override;
    void undo() override;
    void redo() override;

private:
    void imgui(
        const char*                                     stack_label,
        const std::vector<std::shared_ptr<IOperation>>& operations
    );

    Undo_command                             m_undo_command;
    Redo_command                             m_redo_command;

    std::vector<std::shared_ptr<IOperation>> m_executed;
    std::vector<std::shared_ptr<IOperation>> m_undone;
};

IOperation_stack* g_operation_stack{nullptr};

Operation_stack::Operation_stack()
    : erhe::components::Component{c_type_name}
{
}

Operation_stack::~Operation_stack() noexcept
{
    ERHE_VERIFY(g_operation_stack == nullptr);
}

void Operation_stack::deinitialize_component()
{
    m_impl.reset();
}

void Operation_stack::declare_required_components()
{
    require<erhe::application::Commands     >();
    require<erhe::application::Imgui_windows>();
}

void Operation_stack::initialize_component()
{
    m_impl = std::make_unique<Operation_stack_impl>();
}

void Operation_stack_impl::push(const std::shared_ptr<IOperation>& operation)
{
    operation->execute();
    m_executed.push_back(operation);
    m_undone.clear();
}

void Operation_stack_impl::undo()
{
    if (m_executed.empty()) {
        return;
    }
    auto operation = m_executed.back(); // intentionally not a reference, otherwise pop_back() below will invalidate
    m_executed.pop_back();
    operation->undo();
    m_undone.push_back(operation);
}

void Operation_stack_impl::redo()
{
    if (m_undone.empty()) {
        return;
    }
    auto operation = m_undone.back(); // intentionally not a reference, otherwise pop_back() below will invalidate
    m_undone.pop_back();
    operation->execute();
    m_executed.push_back(operation);
}

auto Operation_stack_impl::can_undo() const -> bool
{
    return !m_executed.empty();
}

auto Operation_stack_impl::can_redo() const -> bool
{
    return !m_undone.empty();
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Operation_stack_impl::imgui(
    const char*                                     stack_label,
    const std::vector<std::shared_ptr<IOperation>>& operations
)
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
#endif

void Operation_stack_impl::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    imgui("Executed", m_executed);
    imgui("Undone", m_undone);
#endif
}

} // namespace editor
