#pragma once

#include "tools/tool.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <glm/glm.hpp>

namespace erhe::graphics {
    class Instance;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::scene {
    class Node;
};

namespace editor
{

class Editor_context;
class Editor_message;
class Editor_message_bus;
class Editor_windows;
class Headset_view;
class Icon_set;
class Mesh_memory;
class Rendertarget_imgui_viewport;
class Rendertarget_mesh;
class Scene_builder;
class Tools;
class Viewport_windows;

class Hud;

class Toggle_hud_visibility_command
    : public erhe::commands::Command
{
public:
    Toggle_hud_visibility_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};

class Hud_drag_command
    : public erhe::commands::Command
{
public:
    Hud_drag_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    void try_ready  () override;
    auto try_call   () -> bool override;
    void on_inactive() override;

private:
    Editor_context& m_context;
};

class Hud
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    class Config
    {
    public:
        bool  enabled{true};
        bool  locked {false};
        int   width  {1024};
        int   height {1024};
        float ppm    {5000.0f};
        float x      {0.0f};
        float y      {0.0f};
        float z      {0.0f};
    };
    Config config;

    Hud(
        erhe::commands::Commands&       commands,
        erhe::graphics::Instance&       graphics_instance,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        Editor_context&                 editor_context,
        Editor_message_bus&             editor_message_bus,
        Editor_windows&                 editor_windows,
        Headset_view&                   headset_view,
        Mesh_memory&                    mesh_memory,
        Scene_builder&                  scene_builder,
        Tools&                          tools
    );

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    // Public APi
    [[nodiscard]] auto get_rendertarget_imgui_viewport() const -> std::shared_ptr<Rendertarget_imgui_viewport>;
    auto toggle_visibility() -> bool;
    void set_visibility   (bool value);

    auto try_begin_drag() -> bool;
    void on_drag       ();
    void end_drag      ();

    [[nodiscard]] auto world_from_node() const -> glm::mat4;
    [[nodiscard]] auto node_from_world() const -> glm::mat4;
    [[nodiscard]] auto intersect_ray(
        const glm::vec3& ray_origin_in_world,
        const glm::vec3& ray_direction_in_world
    ) -> std::optional<glm::vec3>;

private:
    void on_message           (Editor_message& message);
    void update_node_transform(const glm::mat4& world_from_camera);

    Toggle_hud_visibility_command m_toggle_visibility_command;

#if defined(ERHE_XR_LIBRARY_OPENXR)
    Hud_drag_command                          m_drag_command;
    erhe::commands::Redirect_command          m_drag_float_redirect_update_command;
    erhe::commands::Drag_enable_float_command m_drag_float_enable_command;
    erhe::commands::Redirect_command          m_drag_bool_redirect_update_command;
    erhe::commands::Drag_enable_command       m_drag_bool_enable_command;
#endif

    std::weak_ptr<erhe::scene::Node>             m_drag_node;

    std::shared_ptr<erhe::scene::Node>           m_rendertarget_node;
    std::shared_ptr<Rendertarget_mesh>           m_rendertarget_mesh;
    std::shared_ptr<Rendertarget_imgui_viewport> m_rendertarget_imgui_viewport;
    glm::mat4                                    m_world_from_camera{1.0f};
    float m_x             {-0.09f};
    float m_y             { 0.0f};
    float m_z             {-0.38f};
    int   m_width_items   {10};
    int   m_height_items  {10};
    bool  m_enabled       {true};
    bool  m_locked_to_head{false};

    std::optional<glm::mat4> m_node_from_control;
};

} // namespace editor
