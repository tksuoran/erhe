#include "tools.hpp"
#include "operations/operation_stack.hpp"
#include "operations/ioperation.hpp"

namespace editor
{

Operation_stack::Operation_stack()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Operation_stack::~Operation_stack() = default;

void Operation_stack::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Operation_stack::push(std::shared_ptr<IOperation> operation)
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

void Operation_stack::imgui(
    const char*                                     stack_label,
    const std::vector<std::shared_ptr<IOperation>>& operations
)
{
    //const ImGuiTreeNodeFlags parent_flags{
    //    ImGuiTreeNodeFlags_OpenOnArrow       |
    //    ImGuiTreeNodeFlags_OpenOnDoubleClick |
    //    ImGuiTreeNodeFlags_SpanFullWidth
    //};
    const ImGuiTreeNodeFlags leaf_flags{
        ImGuiTreeNodeFlags_SpanFullWidth    |
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_Leaf
    };

    ImGui::Begin(stack_label);

    for (const auto& op : operations)
    {
        ImGui::TreeNodeEx(
            op->describe().c_str(),
            leaf_flags
        );
    }
    ImGui::End();
}

void Operation_stack::imgui(Pointer_context& /*pointer_context*/)
{
    imgui("Executed", m_executed);
    imgui("Undone", m_undone);

}


}
