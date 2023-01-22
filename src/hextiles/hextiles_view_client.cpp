#include "hextiles_view_client.hpp"

#include "map_window.hpp"
#include "tile_renderer.hpp"

#include "erhe/application/application_view.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"

namespace hextiles
{

Hextiles_view_client::Hextiles_view_client()
    : Component{c_type_name}
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
    erhe::application::g_view->set_client(this);
}

void Hextiles_view_client::update()
{
    // TODO Use more render graph
    g_map_window->render();
    erhe::application::g_imgui_windows->imgui_windows();
    if (erhe::application::g_rendergraph != nullptr)
    {
        erhe::application::g_rendergraph->execute();
    }
    erhe::application::g_imgui_renderer->next_frame();
    g_tile_renderer ->next_frame();
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

