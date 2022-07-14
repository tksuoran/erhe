#include "view_client.hpp"
#include "editor_rendering.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/scene/scene.hpp"

namespace editor
{

View_client::View_client()
    : Component{c_label}
{
}

View_client::~View_client() noexcept
{
}

void View_client::declare_required_components()
{
    require<erhe::application::View>();
}

void View_client::initialize_component()
{
    get<erhe::application::View>()->set_client(this);
}

void View_client::post_initialize()
{
    m_imgui_windows    = get<erhe::application::Imgui_windows>();
    m_editor_rendering = get<Editor_rendering                >();
    m_pointer_context  = get<Pointer_context                 >();
    m_scene_root       = get<Scene_root                      >();
    m_viewport_windows = get<Viewport_windows                >();
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
    m_viewport_windows->update_viewport_windows();
    if (m_scene_root)
    {
        m_scene_root->scene().update_node_transforms();
    }
}

void View_client::render()
{
    erhe::graphics::Scoped_debug_group frame_scope{"frame"};
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

