#pragma once

#include "erhe/application/view.hpp"
#include "erhe/components/components.hpp"

namespace erhe::application
{
    class Commands;
    class Configuration;
    class Imgui_renderer;
    class Rendergraph;
}

namespace editor
{

class Editor_message_bus;
class Editor_rendering;
class Editor_scenes;
class Hotbar;
class Hud;
class Scene_message_bus;
class Tools;
class Trs_tool;
class Viewport_windows;

class Editor_view_client
    : public erhe::application::View_client
    , public erhe::components::Component
    , public erhe::components::IUpdate_fixed_step
{
public:
    static constexpr std::string_view c_type_name{"editor::View_client"};
    static constexpr uint32_t         c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Editor_view_client ();
    ~Editor_view_client() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements erhe::components::IUpdate_fixed_step
    void update_fixed_step(const erhe::components::Time_context&) override;

    // Implements erhe::application::View_client
    void update() override;
    void update_keyboard(
        const bool                   pressed,
        const erhe::toolkit::Keycode code,
        const uint32_t               modifier_mask
    ) override;
    void update_mouse(const erhe::toolkit::Mouse_button button, const int count) override;
    void update_mouse(const double x, const double y) override;

private:
    std::shared_ptr<erhe::application::Commands      > m_commands;
    std::shared_ptr<erhe::application::Configuration > m_configuration;
    std::shared_ptr<erhe::application::Imgui_windows > m_imgui_windows;
    std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;
    std::shared_ptr<erhe::application::Rendergraph   > m_render_graph;

    std::shared_ptr<Editor_message_bus> m_editor_message_bus;
    std::shared_ptr<Editor_rendering  > m_editor_rendering;
    std::shared_ptr<Editor_scenes     > m_editor_scenes;
    std::shared_ptr<Viewport_windows  > m_viewport_windows;
    std::shared_ptr<Hotbar            > m_hotbar;
    std::shared_ptr<Hud               > m_hud;
    std::shared_ptr<Scene_message_bus > m_scene_message_bus;
    std::shared_ptr<Trs_tool          > m_trs_tool;
    std::shared_ptr<Tools             > m_tools;
};

} // namespace editor

