#include "operations/operation_stack.hpp"
#include "operations/ioperation.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

IOperation::~IOperation() noexcept
{
}

auto Undo_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    if (m_operation_stack.can_undo())
    {
        m_operation_stack.undo();
        return true;
    }
    else
    {
        return false;
    }
}

auto Redo_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    if (m_operation_stack.can_redo())
    {
        m_operation_stack.redo();
        return true;
    }
    else
    {
        return false;
    }
}

Operation_stack::Operation_stack()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
    , m_undo_command                 {*this}
    , m_redo_command                 {*this}
{
}

Operation_stack::~Operation_stack() noexcept
{
}

void Operation_stack::declare_required_components()
{
    require<erhe::application::Commands     >();
    require<erhe::application::Imgui_windows>();
}

void Operation_stack::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    const auto commands = get<erhe::application::Commands>();
    commands->register_command(&m_undo_command);
    commands->register_command(&m_redo_command);
    commands->bind_command_to_key(&m_undo_command, erhe::toolkit::Key_z, true, erhe::toolkit::Key_modifier_bit_ctrl);
    commands->bind_command_to_key(&m_redo_command, erhe::toolkit::Key_y, true, erhe::toolkit::Key_modifier_bit_ctrl);
}

auto Operation_stack::description() -> const char*
{
    return c_title.data();
}

void Operation_stack::push(const std::shared_ptr<IOperation>& operation)
{
    Operation_context context
    {
        .components = m_components
    };
    operation->execute(context);
    m_executed.push_back(operation);
    m_undone.clear();
}

void Operation_stack::undo()
{
    if (m_executed.empty())
    {
        return;
    }
    auto operation = m_executed.back(); // intentionally not a reference, otherwise pop_back() below will invalidate
    m_executed.pop_back();

    Operation_context context
    {
        .components = m_components
    };

    operation->undo(context);
    m_undone.push_back(operation);
}

void Operation_stack::redo()
{
    if (m_undone.empty())
    {
        return;
    }
    auto operation = m_undone.back(); // intentionally not a reference, otherwise pop_back() below will invalidate
    m_undone.pop_back();

    Operation_context context
    {
        .components = m_components
    };

    operation->execute(context);

    m_executed.push_back(operation);
}

auto Operation_stack::can_undo() const -> bool
{
    return !m_executed.empty();
}

auto Operation_stack::can_redo() const -> bool
{
    return !m_undone.empty();
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Operation_stack::imgui(
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

    if (ImGui::TreeNodeEx(stack_label, parent_flags))
    {
        for (const auto& op : operations)
        {
            ImGui::TreeNodeEx(
                op->describe().c_str(),
                leaf_flags
            );
        }
        ImGui::TreePop();
    }
}
#endif

void Operation_stack::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    imgui("Executed", m_executed);
    imgui("Undone", m_undone);
#endif
}

} // namespace editor
