#include "view_client.hpp"
#include "editor_rendering.hpp"
#include "tools/pointer_context.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"

namespace editor
{

View_client::View_client()
    : Component{c_name}
{
}

View_client::~View_client()
{

}

void View_client::connect()
{
    m_imgui_windows    = get<erhe::application::Imgui_windows>();
    m_editor_rendering = get<Editor_rendering                >();
    m_pointer_context  = get<Pointer_context                 >();
    require<erhe::application::View>();
}

void View_client::initialize_component()
{
    get<erhe::application::View>()->set_client(this);
}

void View_client::initial_state()
{
    m_editor_rendering->init_state();
}

void View_client::bind_default_framebuffer()
{
    m_editor_rendering->bind_default_framebuffer();
}

void View_client::clear()
{
    m_editor_rendering->clear();
}

void View_client::update()
{
}

void View_client::render()
{
    m_editor_rendering->render();
}

void View_client::update_imgui_window(
    erhe::application::Imgui_window* imgui_window
)
{
    auto* viewport_window = as_viewport_window(imgui_window);
    m_pointer_context->update_viewport(viewport_window);
}

void View_client::update_keyboard(
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    m_pointer_context->update_keyboard(pressed, code, modifier_mask);
}

void View_client::update_mouse(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    m_pointer_context->update_mouse(button, count);
}

void View_client::update_mouse(const double x, const double y)
{
    m_pointer_context->update_mouse(x, y);
}

} // namespace hextiles

