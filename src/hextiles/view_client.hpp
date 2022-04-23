#pragma once

#include "erhe/application/view.hpp"
#include "erhe/components/components.hpp"

namespace erhe::graphics
{
    class OpenGL_state_tracker;
}

namespace hextiles
{

class Map_renderer;
class Map_window;

class View_client
    : public erhe::application::View_client
    , public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"hextiles::View_client"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    View_client ();
    ~View_client () noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements erhe::application::View_client
    void initial_state           () override;
    void update                  () override;
    void render                  () override;
    void bind_default_framebuffer() override;
    void clear                   () override;
    void update_imgui_window(erhe::application::Imgui_window* imgui_window) override;
    void update_keyboard(
        const bool                   pressed,
        const erhe::toolkit::Keycode code,
        const uint32_t               modifier_mask
    ) override;
    void update_mouse(const erhe::toolkit::Mouse_button button, const int count) override;
    void update_mouse(const double x, const double y) override;

private:
    std::shared_ptr<erhe::application::Imgui_windows>     m_imgui_windows;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Map_renderer> m_map_renderer;
    std::shared_ptr<Map_window  > m_map_window;
};

} // namespace hextiles

