#pragma once

#include "windows/imgui_window.hpp"

#include <memory>

namespace editor
{

class Scene_root;
class Selection_tool;

class Camera_properties
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name{"Camera_properties"};
    Camera_properties();
    ~Camera_properties() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

private:
    std::shared_ptr<Scene_root>     m_scene_root;
    std::shared_ptr<Selection_tool> m_selection_tool;
};

} // namespace editor
