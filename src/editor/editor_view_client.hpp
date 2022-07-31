#pragma once

#include "erhe/application/view.hpp"
#include "erhe/components/components.hpp"

namespace erhe::application
{
    class Imgui_renderer;
    class Render_graph;
}

namespace editor
{

class Editor_rendering;
class Editor_scenes;
class Viewport_windows;

class Editor_view_client
    : public erhe::application::View_client
    , public erhe::components::Component
    , public erhe::components::IUpdate_fixed_step
{
public:
    static constexpr std::string_view c_label{"editor::View_client"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Editor_view_client ();
    ~Editor_view_client() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
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
    std::shared_ptr<erhe::application::Imgui_windows > m_imgui_windows;
    std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;
    std::shared_ptr<erhe::application::Render_graph  > m_render_graph;

    std::shared_ptr<Editor_rendering> m_editor_rendering;
    std::shared_ptr<Editor_scenes   > m_editor_scenes;
    std::shared_ptr<Viewport_windows> m_viewport_windows;
};

} // namespace editor

