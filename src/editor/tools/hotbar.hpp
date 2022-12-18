#pragma once

#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

namespace erhe::application
{
    class Imgui_renderer;
}

namespace erhe::scene
{
    class Camera;
    class Node;
}

namespace editor
{

class Brush_tool;
class Editor_message;
class Icon_set;
class Operations;
class Physics_tool;
class Rendertarget_imgui_viewport;
class Rendertarget_mesh;
class Selection_tool;
class Trs_tool;
class Viewport_windows;

using Action = int;
class Hotbar_action
{
public:
    static constexpr Action First        = 0;
    static constexpr Action Select       = 0;
    static constexpr Action Move         = 1;
    static constexpr Action Rotate       = 2;
    static constexpr Action Drag         = 3;
    static constexpr Action Brush_tool   = 4;
    static constexpr Action Last         = 4;
};

class Hotbar;

class Hotbar_trackpad_command
    : public erhe::application::Command
{
public:
    explicit Hotbar_trackpad_command(Hotbar& hotbar)
        : Command {"Hotbar.trackpad"}
        , m_hotbar{hotbar}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Hotbar& m_hotbar;
};

class Hotbar
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public Tool
{
public:
    static constexpr std::string_view c_type_name{"Hotbar"};
    static constexpr std::string_view c_title{"Hotbar"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Hotbar ();
    ~Hotbar() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context)  override;

    // Implements Imgui_window
    [[nodiscard]] auto flags() -> ImGuiWindowFlags override;
    void on_begin() override;
    void imgui   () override;

    // Public API
    auto try_call (erhe::application::Command_context& context) -> bool;
    auto get_color(int color) -> glm::vec4&;

private:
    void on_message           (Editor_message& message);
    void update_node_transform(const glm::mat4& world_from_camera);
    void set_action           (Action action);
    void button               (glm::vec2 icon, Action action, const char* tooltip);

    [[nodiscard]] auto get_camera() const -> std::shared_ptr<erhe::scene::Camera>;

    // Commands
    Hotbar_trackpad_command m_trackpad_command;

    // Component dependencies
    std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;
    std::shared_ptr<Brush_tool>                        m_brush_tool;
    std::shared_ptr<Icon_set>                          m_icon_set;
    std::shared_ptr<Operations>                        m_operations;
    std::shared_ptr<Physics_tool>                      m_physics_tool;
    std::shared_ptr<Selection_tool>                    m_selection_tool;
    std::shared_ptr<Trs_tool>                          m_trs_tool;
    std::shared_ptr<Viewport_windows>                  m_viewport_windows;

    std::shared_ptr<erhe::scene::Node>           m_rendertarget_node;
    std::shared_ptr<Rendertarget_mesh>           m_rendertarget_mesh;
    std::shared_ptr<Rendertarget_imgui_viewport> m_rendertarget_imgui_viewport;

    bool  m_show{true};
    float m_x{ 0.0f};
    float m_y{ 0.07f};
    float m_z{-0.4f};

    glm::vec4 m_color_active  {0.3f, 0.3f, 0.8f, 0.8f};
    glm::vec4 m_color_hover   {0.4f, 0.4f, 0.4f, 0.8f};
    glm::vec4 m_color_inactive{0.1f, 0.1f, 0.4f, 0.8f};

    Action m_action{Hotbar_action::Select};
};

} // namespace editor
