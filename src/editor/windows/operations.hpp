#pragma once

#include "windows/window.hpp"
#include "editor.hpp"

#include <memory>

namespace editor
{

class Operation_stack;
class Selection_tool;
class Scene_manager;

class Operations
    : public Window
{
public:
    Operations(Editor&                                 editor,
               const std::shared_ptr<Operation_stack>& operation_stack,
               const std::shared_ptr<Selection_tool>&  selection_tool,
               const std::shared_ptr<Scene_manager>&   scene_manager);

    virtual ~Operations() = default;

    void window(Pointer_context& pointer_context) override;

private:
    Editor&                          m_editor;
    std::shared_ptr<Operation_stack> m_operation_stack;
    std::shared_ptr<Selection_tool>  m_selection_tool;
    std::shared_ptr<Scene_manager>   m_scene_manager;
};

} // namespace editor
