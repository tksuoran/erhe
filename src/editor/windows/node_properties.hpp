#pragma once

#include "windows/imgui_window.hpp"

#include <memory>

namespace editor
{

class Scene_manager;
class Selection_tool;

class Node_properties
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name{"Node_properties"};

    Node_properties ();
    ~Node_properties() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Scene_manager>  m_scene_manager;
    std::shared_ptr<Selection_tool> m_selection_tool;
};

} // namespace editor
