#include "editor_tools.hpp"
#include "editor_view.hpp"
#include "operations/operation_stack.hpp"
#include "operations/ioperation.hpp"

namespace editor
{

auto Undo_command::try_call(Command_context& context) -> bool
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

auto Redo_command::try_call(Command_context& context) -> bool
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
    : erhe::components::Component{c_name}
    , Imgui_window               {c_description}
    , m_undo_command             {*this}
    , m_redo_command             {*this}
{
}

Operation_stack::~Operation_stack() = default;

void Operation_stack::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);

    hide();

    const auto view = get<Editor_view>();
    view->register_command(&m_undo_command);
    view->register_command(&m_redo_command);
    using Keycode = erhe::toolkit::Keycode;
    view->bind_command_to_key(&m_undo_command, Keycode::Key_z, true, erhe::toolkit::Key_modifier_bit_ctrl);
    view->bind_command_to_key(&m_redo_command, Keycode::Key_y, true, erhe::toolkit::Key_modifier_bit_ctrl);
}

auto Operation_stack::description() -> const char*
{
    return c_description.data();
}

void Operation_stack::push(const std::shared_ptr<IOperation>& operation)
{
    operation->execute();
    m_executed.push_back(operation);
    m_undone.clear();
}

void Operation_stack::undo()
{
    if (m_executed.empty())
    {
        return;
    }
    auto operation = m_executed.back();
    m_executed.pop_back();
    operation->undo();
    m_undone.push_back(operation);
}

void Operation_stack::redo()
{
    if (m_undone.empty())
    {
        return;
    }
    auto operation = m_undone.back();
    m_undone.pop_back();
    operation->execute();
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

void Operation_stack::imgui()
{
    imgui("Executed", m_executed);
    imgui("Undone", m_undone);
}


}
