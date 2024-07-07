#pragma once

#include "tools/tool.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::commands {
    class Commands;
}
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
    class Camera;
    class Mesh;
    class Node;
}

namespace editor {

class Editor_context;
class Editor_message_bus;
class Hotbar;
class Icon_set;
class Mesh_memory;
class Rendertarget_imgui_viewport;
class Rendertarget_mesh;
class Scene_builder;
class Scene_root;
class Tools;
class Viewport_windows;
class Headset_view;

class Toggle_menu_visibility_command : public erhe::commands::Command
{
public:
    Toggle_menu_visibility_command(erhe::commands::Commands& commands, Editor_context& context);
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};

class Hotbar_trackpad_command : public erhe::commands::Command
{
public:
    Hotbar_trackpad_command(erhe::commands::Commands& commands, Editor_context& context);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Editor_context& m_context;
};

class Hotbar
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    Hotbar(
        erhe::commands::Commands&       commands,
        erhe::graphics::Instance&       graphics_instance,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        Editor_context&                 editor_context,
        Editor_message_bus&             editor_message_bus,
        Headset_view&                   headset_view,
        Icon_set&                       icon_set,
        Mesh_memory&                    mesh_memory,
        Scene_builder&                  scene_builder,
        Tools&                          tools
    );

    // Implements Tool
    void tool_render(const Render_context& context)  override;

    void get_all_tools();

    // Implements Imgui_window
    auto flags   () -> ImGuiWindowFlags override;
    void on_begin() override;
    void imgui   () override;

    // Public API
    auto try_call         (erhe::commands::Input_arguments& input) -> bool;
    auto get_color        (int color) -> glm::vec4&;
    auto toggle_visibility() -> bool;
    void set_visibility   (bool value);
    auto get_position     () const -> glm::vec3;
    void set_position     (glm::vec3 position);
    auto get_locked       () const -> bool;
    void set_locked       (bool value);

private:
    void on_message            (Editor_message& message);
    void update_node_transform ();
    void tool_button           (uint32_t id, Tool* tool);
    void handle_slot_update    ();

    void init_hotbar();
    void init_radial_menu(Mesh_memory& mesh_memory, Scene_root& scene_root);

    [[nodiscard]] auto get_camera() const -> std::shared_ptr<erhe::scene::Camera>;

    // Commands
    Toggle_menu_visibility_command            m_toggle_visibility_command;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    Hotbar_trackpad_command                   m_trackpad_command;
    erhe::commands::Xr_vector2f_click_command m_trackpad_click_command;
#endif

    std::shared_ptr<erhe::scene::Node>              m_rendertarget_node;
    std::shared_ptr<Rendertarget_mesh>              m_rendertarget_mesh;
    std::shared_ptr<Rendertarget_imgui_viewport>    m_rendertarget_imgui_viewport;
    //Scene_view*                                     m_hover_scene_view{nullptr};

    std::shared_ptr<erhe::scene::Node>              m_radial_menu_node;
    std::shared_ptr<erhe::scene::Mesh>              m_radial_menu_background_mesh;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> m_radial_menu_icons;

    bool  m_enabled   {true};
    bool  m_show      {true};
    bool  m_use_radial{true};
    bool  m_locked    {false};
    float m_x         { 0.0f};
    float m_y         { 0.07f};
    float m_z         {-0.4f};

    std::size_t        m_slot      {0};
    std::size_t        m_slot_first{0};
    std::size_t        m_slot_last {0};
    std::vector<Tool*> m_slots;

    glm::vec4 m_color_active  {0.3f, 0.3f, 0.8f, 0.8f};
    glm::vec4 m_color_hover   {0.4f, 0.4f, 0.6f, 0.8f};
    glm::vec4 m_color_inactive{0.1f, 0.1f, 0.4f, 0.8f};
};

} // namespace editor
