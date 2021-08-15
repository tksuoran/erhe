#pragma once

#include "tools/pointer_context.hpp"
#include "erhe/components/component.hpp"
#include "erhe/toolkit/view.hpp"

namespace editor {

class Editor_rendering;
class Editor_time;
class Editor_tools;
class Fly_camera_tool;
class Id_renderer;
class Operation_stack;
class Scene_manager;
class Scene_root;

class Editor_view
    : public erhe::components::Component
    , public erhe::toolkit::View
{
public:
    static constexpr std::string_view c_name{"Editor_view"};
    Editor_view();
    ~Editor_view() override;

    // Implements Component
    void connect();

    // Implements View
    void update        () override;
    void on_enter      () override;
    void on_mouse_move (double x, double y) override;
    void on_mouse_click(erhe::toolkit::Mouse_button button, int count) override;
    void on_key_press  (erhe::toolkit::Keycode code, uint32_t modifier_mask) override;
    void on_key_release(erhe::toolkit::Keycode code, uint32_t modifier_mask) override;

    void on_key        (bool pressed, erhe::toolkit::Keycode code, uint32_t modifier_mask);
    void update_pointer();

    Pointer_context pointer_context;

private:
    std::shared_ptr<Editor_rendering> m_editor_rendering;
    std::shared_ptr<Editor_time>      m_editor_time;
    std::shared_ptr<Editor_tools>     m_editor_tools;
    std::shared_ptr<Fly_camera_tool>  m_fly_camera_tool;
    std::shared_ptr<Id_renderer>      m_id_renderer;
    std::shared_ptr<Operation_stack>  m_operation_stack;
    std::shared_ptr<Scene_manager>    m_scene_manager;
    std::shared_ptr<Scene_root>       m_scene_root;
};

}
