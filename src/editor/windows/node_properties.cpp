#include "windows/node_properties.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/scene/node.hpp"

#include "imgui.h"

namespace editor
{

Node_properties::Node_properties(const std::shared_ptr<Scene_manager>&  scene_manager,
                                 const std::shared_ptr<Selection_tool>& selection_tool)
    : m_scene_manager {scene_manager}
    , m_selection_tool{selection_tool}
{
}

void Node_properties::window(Pointer_context&)
{
}

}
