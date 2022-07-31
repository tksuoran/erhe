#include "hextiles_view_client.hpp"
#include "map_window.hpp"
#include "tile_renderer.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"

namespace hextiles
{

Hextiles_view_client::Hextiles_view_client()
    : Component{c_label}
{
}

Hextiles_view_client::~Hextiles_view_client() noexcept
{
}

void Hextiles_view_client::declare_required_components()
{
    require<erhe::application::View>();
}

void Hextiles_view_client::initialize_component()
{
    get<erhe::application::View>()->set_client(this);
}

void Hextiles_view_client::post_initialize()
{
    m_imgui_windows          = get<erhe::application::Imgui_windows    >();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();

    m_tile_renderer = get<Tile_renderer>();
    m_map_window    = get<Map_window   >();
}

void Hextiles_view_client::update()
{
    // TODO Use render graph?
    m_map_window->render();
    m_tile_renderer->next_frame();
}

void Hextiles_view_client::update_keyboard(
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    static_cast<void>(pressed);
    static_cast<void>(code);
    static_cast<void>(modifier_mask);
}

void Hextiles_view_client::update_mouse(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    static_cast<void>(button);
    static_cast<void>(count);
}

void Hextiles_view_client::update_mouse(const double x, const double y)
{
    static_cast<void>(x);
    static_cast<void>(y);
}

} // namespace hextiles

