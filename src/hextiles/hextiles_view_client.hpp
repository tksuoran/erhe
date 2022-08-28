#pragma once

#include "erhe/application/view.hpp"
#include "erhe/components/components.hpp"

namespace erhe::application {

class Imgui_renderer;
class Imgui_windows;
class Rendergraph;

}

namespace erhe::graphics
{
    class OpenGL_state_tracker;
}

namespace hextiles
{

class Tile_renderer;
class Map_window;

class Hextiles_view_client
    : public erhe::application::View_client
    , public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"hextiles::View_client"};
    static constexpr uint32_t         c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Hextiles_view_client ();
    ~Hextiles_view_client() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

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
    std::shared_ptr<erhe::application::Imgui_renderer>    m_imgui_renderer;
    std::shared_ptr<erhe::application::Imgui_windows>     m_imgui_windows;
    std::shared_ptr<erhe::application::Rendergraph>       m_rendergraph;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Tile_renderer> m_tile_renderer;
    std::shared_ptr<Map_window  > m_map_window;
};

} // namespace hextiles

