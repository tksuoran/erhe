#pragma once

#include "windows/window.hpp"

#include <memory>

namespace editor
{

class Scene_root;
class Selection_tool;

class Material_properties
    : public erhe::components::Component
    , public Window
{
public:
    static constexpr std::string_view c_name{"Light_properties"};

    Material_properties ();
    ~Material_properties() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Window
    void window(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Scene_root>     m_scene_root;
    std::shared_ptr<Selection_tool> m_selection_tool;
    int                             m_material_index{0};
};

} // namespace editor
