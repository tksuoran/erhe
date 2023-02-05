#pragma once

#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

namespace erhe::scene
{
    class Node;
};

namespace editor
{

class Editor_message;
class Rendertarget_imgui_viewport;
class Rendertarget_mesh;

class Hud;

class Toggle_hud_visibility_command
    : public erhe::application::Command
{
public:
    Toggle_hud_visibility_command();
    auto try_call() -> bool override;
};

class Hud_drag_command
    : public erhe::application::Command
{
public:
    Hud_drag_command();
    void try_ready  () override;
    auto try_call   () -> bool override;
    void on_inactive() override;
};

class Hud
    : public erhe::components::Component
    , public erhe::application::Imgui_window
    , public Tool
{
public:
    static constexpr std::string_view c_type_name{"Hud"};
    static constexpr std::string_view c_title{"Hud"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Hud ();
    ~Hud() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    // Public APi
    [[nodiscard]] auto get_rendertarget_imgui_viewport() const -> std::shared_ptr<Rendertarget_imgui_viewport>;
    auto toggle_visibility    () -> bool;
    void set_visibility       (bool value);

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
    Hud_drag_command                             m_drag_command;
    erhe::application::Redirect_command          m_drag_float_redirect_update_command;
    erhe::application::Drag_enable_float_command m_drag_float_enable_command;
    erhe::application::Redirect_command          m_drag_bool_redirect_update_command;
    erhe::application::Drag_enable_command       m_drag_bool_enable_command;
#endif

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
    bool  m_is_visible    {false};
    bool  m_locked_to_head{false};

    std::optional<glm::mat4> m_node_from_control;
};

extern Hud* g_hud;

} // namespace editor
