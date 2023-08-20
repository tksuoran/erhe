#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor
{

class Editor_context;
class Editor_message_bus;
class Content_library;
class Scene_root;

class Animation_window
    : public erhe::imgui::Imgui_window
{
public:
    Animation_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Editor_message_bus&          editor_message_bus
    );

    // Implements Imgui_window
    void imgui() override;

private:
    Editor_context& m_context;

    float m_start_time{0.0f};
    float m_end_time  {4.0f};
    float m_time      {0.0f};
    float m_speed     {1.0f};
    std::weak_ptr<Content_library> m_content_library;
    std::weak_ptr<Scene_root>      m_scene_root;
};

} // namespace editor
