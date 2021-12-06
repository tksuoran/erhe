#pragma once

#include "tools/pointer_context.hpp"

#include "erhe/components/component.hpp"
#include "erhe/toolkit/view.hpp"

namespace editor {

class Editor_rendering;
class Editor_time;
class Editor_tools;
class Fly_camera_tool;
class Frame_log_window;
class Id_renderer;
class Operation_stack;
class Scene_manager;
class Scene_root;
class Viewport_windows;
class Window;

class Editor_view
    : public erhe::components::Component
    , public erhe::toolkit::View
{
public:
    static constexpr std::string_view c_name{"Editor_view"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Editor_view();
    ~Editor_view() override;

    // Implements Component
    auto get_type_hash() const -> uint32_t override { return hash; }
    void connect      () override;

    // Implements View
    void update        () override;
    void on_enter      () override;
    void on_exit       () override;
    void on_mouse_move (const double x, const double y) override;
    void on_mouse_click(const erhe::toolkit::Mouse_button button, const int count) override;
    void on_key_press  (const erhe::toolkit::Keycode code, const uint32_t modifier_mask) override;
    void on_key_release(const erhe::toolkit::Keycode code, const uint32_t modifier_mask) override;
    void on_key        (bool pressed, erhe::toolkit::Keycode code, uint32_t modifier_mask);

private:
    Editor_rendering* m_editor_rendering{nullptr};
    Editor_time*      m_editor_time     {nullptr};
    Editor_tools*     m_editor_tools    {nullptr};
    Fly_camera_tool*  m_fly_camera_tool {nullptr};
    Frame_log_window* m_frame_log_window{nullptr};
    Operation_stack*  m_operation_stack {nullptr};
    Pointer_context*  m_pointer_context {nullptr};
    Scene_manager*    m_scene_manager   {nullptr};
    Scene_root*       m_scene_root      {nullptr};
    Viewport_windows* m_viewport_windows{nullptr};
    Window*           m_window          {nullptr};
};

}
