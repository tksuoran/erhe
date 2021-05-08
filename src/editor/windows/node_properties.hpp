#pragma once

#include "windows/window.hpp"

#include <memory>

namespace editor
{

class Scene_manager;
class Selection_tool;

class Node_properties
    : public erhe::components::Component
    , public Window
{
public:
    static constexpr const char* c_name = "Node_properties";
    Node_properties() : erhe::components::Component(c_name) {}
    virtual ~Node_properties() = default;

    // Implements Component
    void connect() override;

    // Implements Window
    void window(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Scene_manager>  m_scene_manager;
    std::shared_ptr<Selection_tool> m_selection_tool;
};

} // namespace editor
