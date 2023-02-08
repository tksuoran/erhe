#pragma once

#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

namespace erhe::scene
{
    class Camera;
    class Node;
}

namespace editor
{

class Rendertarget_imgui_viewport;
class Rendertarget_mesh;
class Hotbar;

class Hotbar_trackpad_command
    : public erhe::application::Command
{
public:
    Hotbar_trackpad_command();
    auto try_call_with_input(erhe::application::Input_arguments& input) -> bool override;
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
    void deinitialize_component     () override;
    void post_initialize            () override;

    // Implements Tool
    void tool_render(const Render_context& context)  override;

    // Implements Imgui_window
    [[nodiscard]] auto flags() -> ImGuiWindowFlags override;
    void on_begin() override;
    void imgui   () override;

    // Public API
    auto try_call      (erhe::application::Input_arguments& input) -> bool;
    auto get_color     (int color) -> glm::vec4&;
    void set_visibility(bool value);
    auto get_position  () const -> glm::vec3;
    void set_position  (glm::vec3 position);
    auto get_locked    () const -> bool;
    void set_locked    (bool value);

private:
    void on_message           (Editor_message& message);
    void update_node_transform(const glm::mat4& world_from_camera);
    void tool_button          (uint32_t id, Tool* tool);
    void handle_slot_update   ();

    [[nodiscard]] auto get_camera() const -> std::shared_ptr<erhe::scene::Camera>;

    // Commands
#if defined(ERHE_XR_LIBRARY_OPENXR)
    Hotbar_trackpad_command                      m_trackpad_command;
    erhe::application::Xr_vector2f_click_command m_trackpad_click_command;
#endif

    std::shared_ptr<erhe::scene::Node>           m_rendertarget_node;
    std::shared_ptr<Rendertarget_mesh>           m_rendertarget_mesh;
    std::shared_ptr<Rendertarget_imgui_viewport> m_rendertarget_imgui_viewport;
    Scene_view*                                  m_hover_scene_view{nullptr};

    bool  m_enabled{true};
    bool  m_show   {true};
    bool  m_locked {false};
    float m_x      { 0.0f};
    float m_y      { 0.07f};
    float m_z      {-0.4f};

    std::size_t        m_slot      {0};
    std::size_t        m_slot_first{0};
    std::size_t        m_slot_last {0};
    std::vector<Tool*> m_slots;

    glm::vec4 m_color_active  {0.3f, 0.3f, 0.8f, 0.8f};
    glm::vec4 m_color_hover   {0.4f, 0.4f, 0.4f, 0.8f};
    glm::vec4 m_color_inactive{0.1f, 0.1f, 0.4f, 0.8f};
};

extern Hotbar* g_hotbar;

} // namespace editor
