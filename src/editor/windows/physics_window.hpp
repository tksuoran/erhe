#pragma once

#include "tools/tool.hpp"
#include "windows/window.hpp"

#include <memory>

namespace editor
{

class Selection_tool;
class Scene_manager;

class Physics_window
    : public erhe::components::Component
    , public Tool
    , public Window
{
public:
    static constexpr const char* c_name = "Physics_window";
    Physics_window() : erhe::components::Component(c_name) {}
    virtual ~Physics_window() = default;

    // Implements Component
    void connect() override;

    // Implements Tool
    void render(Render_context& render_context) override;
    auto state() const -> State override;
    auto description() -> const char* override { return c_name; }

    // Implements Window
    void window(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Selection_tool> m_selection_tool;
    std::shared_ptr<Scene_manager>  m_scene_manager;
    bool                            m_debug_draw{false};
};

} // namespace editor
