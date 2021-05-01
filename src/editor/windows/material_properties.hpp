#pragma once

#include "windows/window.hpp"

#include <memory>

namespace sample
{

class Scene_manager;
class Selection_tool;

class Material_properties
    : public Window
{
public:
    Material_properties(const std::shared_ptr<Scene_manager>&  scene_manager,
                        const std::shared_ptr<Selection_tool>& selection_tool);

    virtual ~Material_properties() = default;

    void window(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Scene_manager>  m_scene_manager;
    std::shared_ptr<Selection_tool> m_selection_tool;
    int                             m_material_index{0};
    //int                             m_primitive_index{0};
};

} // namespace sample
