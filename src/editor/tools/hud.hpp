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
class Viewport_windows;

class Hud;

class Toggle_hud_visibility_command
    : public erhe::application::Command
{
public:
    explicit Toggle_hud_visibility_command(Hud& hud)
        : Command{"Hud.toggle_visibility"}
        , m_hud  {hud}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Hud& m_hud;
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
    void post_initialize            () override;

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    // Public APi
    [[nodiscard]] auto get_rendertarget_imgui_viewport() -> std::shared_ptr<Rendertarget_imgui_viewport>;
    auto toggle_visibility    () -> bool;
    void set_visibility       (bool value);

private:
    void on_message           (Editor_message& message);
    void update_node_transform(const glm::mat4& world_from_camera);

    Toggle_hud_visibility_command m_toggle_visibility_command;

    std::shared_ptr<Viewport_windows> m_viewport_windows;

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
};

} // namespace editor
