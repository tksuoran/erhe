#pragma once

#include "windows/imgui_window.hpp"

#include <memory>

namespace editor
{

class Scene_root;
class Selection_tool;

class Material_properties
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name{"Light_properties"};

    Material_properties ();
    ~Material_properties() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Scene_root>     m_scene_root;
    std::shared_ptr<Selection_tool> m_selection_tool;
    int                             m_material_index{0};
};

} // namespace editor
