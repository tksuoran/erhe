#pragma once

#include "tools/tool.hpp"
#include "tools/tool_window.hpp"

#include "erhe_commands/command.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include "config/generated/operation_params.hpp"
#include "config/generated/hotbar_anchor.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe::commands {
    class Commands;
}
namespace erhe::graphics {
    class Device;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::rendergraph {
    class Rendergraph;
    class Rendergraph_node;
}
namespace erhe::scene {
    class Camera;
    class Mesh;
    class Node;
}

namespace erhe::primitive { class Material; }

namespace erhe::scene_renderer { class Mesh_memory; }

struct Hotbar_config;

namespace editor {

class App_context;
class App_message_bus;
struct Hover_scene_view_message;
struct Render_scene_view_message;
struct Tool_select_message;
class Brush;
class Hotbar;
class Icon_set;
class Quad_view;
class Scene_root;
class Tools;
class Scene_views;
class Headset_view;

class Slot_entry
{
public:
    Tool*                                      tool    {nullptr};
    std::shared_ptr<Brush>                     brush   {};  // Non-null for brush slots
    std::shared_ptr<erhe::primitive::Material> material{}; // Non-null for material slots
    erhe::commands::Command*                   command {nullptr}; // Non-null for operation slots
    Operation_params                           operation_params{}; // Frozen parameter snapshot for the operation slot
};

class Toggle_menu_visibility_command : public erhe::commands::Command
{
public:
    Toggle_menu_visibility_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Hotbar_trackpad_command : public erhe::commands::Command
{
public:
    Hotbar_trackpad_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    App_context& m_context;
};

class Hotbar_rotate_tool_command : public erhe::commands::Command
{
public:
    Hotbar_rotate_tool_command(erhe::commands::Commands& commands, App_context& context, int rotate_direction);
    auto try_call() -> bool override;

private:
    App_context& m_context;
    int m_rotate_direction;
};

// Activates a fixed hotbar slot (Minecraft-style number keys 1..0 -> slots 1..10).
class Hotbar_activate_slot_command : public erhe::commands::Command
{
public:
    Hotbar_activate_slot_command(erhe::commands::Commands& commands, App_context& context, std::size_t slot_index);
    auto try_call() -> bool override;

private:
    App_context& m_context;
    std::size_t  m_slot_index;
};

class Hotbar_thumbstick_command : public erhe::commands::Command
{
public:
    Hotbar_thumbstick_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    App_context& m_context;
    glm::vec2    m_last_value          {0.0f, 0.0f};
    float        m_activate_threshold  {0.6f};
    float        m_deactivate_threshold{0.4f};
    bool         m_left_active         {false};
    bool         m_right_active        {false};
    bool         m_up_active           {false};
    bool         m_down_active         {false};
};

class Hotbar : public Tool
{
public:
    Hotbar(
        const Hotbar_config&               hotbar_config,
        erhe::commands::Commands&          commands,
        erhe::imgui::Imgui_renderer&       imgui_renderer,
        erhe::imgui::Imgui_windows&        imgui_windows,
        App_context&                       context,
        App_message_bus&                   app_message_bus,
        Headset_view&                      headset_view,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        Tools&                             tools
    );
    ~Hotbar() noexcept;

    // Builds the scene-dependent radial menu into the scene. The scene is created
    // later (by the scene.create startup command), so this is called once a scene
    // becomes available rather than at construction.
    void attach_to_scene       (const std::shared_ptr<Scene_root>& scene_root);

    // Releases the scene-dependent content built by attach_to_scene() (the
    // hotbar quad and the radial menu nodes), so the scene the hotbar was
    // homed in can be closed. attach_to_scene() may be called again later
    // (re-homing to another scene).
    void detach_from_scene     ();

    void get_all_tools();
    void set_slots             (const std::vector<Slot_entry>& slots);
    void rebuild_if_needed     ();

    // Number of number-key hotbar slots (Minecraft-style: keys 1..9,0 -> slots 1..10).
    static constexpr std::size_t key_slot_count = 10;

    // Public API
    auto try_trackpad          (erhe::commands::Input_arguments& input) -> bool;
    void rotate_tool           (int direction);
    void activate_slot         (std::size_t index); // select slot by index (0-based); no-op if out of range
    auto get_color             (int color) -> glm::vec4&;
    auto toggle_mesh_visibility() -> bool;
    void set_mesh_visibility   (bool value);
    auto get_position          () const -> glm::vec3;
    void set_position          (glm::vec3 position);
    auto get_locked            () const -> bool;
    void set_locked            (bool value);

private:
    void on_hover_scene_view_message (Hover_scene_view_message& message);
    void on_tool_select_message      (Tool_select_message& message);
    void on_render_scene_view_message(Render_scene_view_message& message);
    void update_node_transform ();
    void slot_button           (uint32_t id, Slot_entry& entry);
    void handle_slot_update    ();
    void update_slot_from_tool (Tool* tool);

    void init_hotbar();
    void init_radial_menu(erhe::scene_renderer::Mesh_memory& mesh_memory, Scene_root& scene_root);

    // Window callbacks
    void             window_imgui   ();
    auto             window_flags   () -> ImGuiWindowFlags;
    void             window_on_begin();
    void             window_on_end  ();

    [[nodiscard]] auto get_camera() const -> std::shared_ptr<erhe::scene::Camera>;

    Tool_window                              m_window;
    erhe::message_bus::Subscription<Hover_scene_view_message>  m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Tool_select_message>       m_tool_select_subscription;
    erhe::message_bus::Subscription<Render_scene_view_message> m_render_scene_view_subscription;

    // Commands
    Toggle_menu_visibility_command            m_toggle_visibility_command;
    Hotbar_rotate_tool_command                m_prev_tool_command;
    Hotbar_rotate_tool_command                m_next_tool_command;
    // One command per number-key slot (keys 1..9,0 -> slots 1..10). unique_ptr so
    // the non-movable Command objects can live in a vector built in the ctor body.
    std::vector<std::unique_ptr<Hotbar_activate_slot_command>> m_activate_slot_commands;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    Hotbar_trackpad_command                   m_trackpad_command;
    erhe::commands::Xr_vector2f_click_command m_trackpad_click_command;
    Hotbar_thumbstick_command                 m_thumbstick_command;
#endif

    Headset_view*                                   m_headset_view{nullptr};
    // Stored at construction so the scene-dependent radial menu can be built later
    // in attach_to_scene() once a scene exists.
    erhe::scene_renderer::Mesh_memory*              m_mesh_memory{nullptr};
    erhe::rendergraph::Rendergraph_node*            m_connected_consumer_node{nullptr};

    // The scene this hotbar is homed in, set by attach_to_scene(). init_hotbar()
    // builds the hotbar quad into this scene; using it (rather than the
    // Scene_builder's default scene) is what lets the hotbar work for a scene that
    // was loaded rather than created by the scene.create startup command.
    std::weak_ptr<Scene_root>                       m_scene_root;

    std::unique_ptr<Quad_view>                      m_quad_view;

    std::shared_ptr<erhe::scene::Node>              m_radial_menu_node;
    std::shared_ptr<erhe::scene::Mesh>              m_radial_menu_background_mesh;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> m_radial_menu_icons;
    bool                                            m_mesh_visible{true};

    bool  m_needs_rebuild{false};
    bool  m_enabled      {true};
    bool  m_show      {true};
    bool  m_use_radial{true};
    bool  m_locked    {false};
    float m_x         { 0.0f};  // horizontal offset (meters); 0 = centered
    float m_y         { 0.07f}; // computed each frame from the camera vertical FOV (see update_node_transform)
    float m_z         {-0.4f};  // distance in front of the camera (meters; negative = forward)
    Hotbar_anchor m_anchor{Hotbar_anchor::bottom}; // resolved from config (platform_default -> bottom desktop / top OpenXR)
    float         m_height{0.06f};                 // apparent height as a fraction of the viewport vertical extent
    float         m_padding{0.0f};                 // extra inward gap from the anchored frustum plane, fraction of viewport vertical extent

    std::size_t        m_slot      {0};
    std::size_t        m_slot_first{0};
    std::size_t        m_slot_last {0};
    std::vector<Slot_entry> m_slots;

    glm::vec4 m_color_active  {0.3f, 0.3f, 0.8f, 0.8f};
    glm::vec4 m_color_hover   {0.4f, 0.4f, 0.6f, 0.8f};
    glm::vec4 m_color_inactive{0.1f, 0.1f, 0.4f, 0.8f};
};

}
