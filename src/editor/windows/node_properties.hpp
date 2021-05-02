#pragma once

#include "windows/window.hpp"

#include <memory>

namespace editor
{

class Scene_manager;
class Selection_tool;

class Node_properties
    : public Window
{
public:
    Node_properties(const std::shared_ptr<Scene_manager>&  scene_manager,
                    const std::shared_ptr<Selection_tool>& selection_tool);

    virtual ~Node_properties() = default;

    void window(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Scene_manager>  m_scene_manager;
    std::shared_ptr<Selection_tool> m_selection_tool;
};

} // namespace editor
