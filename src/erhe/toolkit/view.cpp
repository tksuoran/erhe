#include "erhe/toolkit/view.hpp"
#include "erhe/toolkit/window.hpp"

namespace erhe::toolkit
{

Root_view::Root_view(Context_window* window)
    : m_window{window}
{
}

void Root_view::set_view(View* view)
{
    view->on_resize(m_window->get_width(), m_window->get_height());
    m_view = view;
}

void Root_view::reset_view(View* view)
{
    view->on_resize(m_window->get_width(), m_window->get_height());
    m_view = view;
    m_last_view = nullptr;
}

void Root_view::on_idle()
{
    if (m_last_view != m_view)
    {
        if (m_last_view)
        {
            m_last_view->on_exit();
        }

        m_view->on_enter();
        m_last_view = m_view;
    }

    if (m_view != nullptr)
    {
        m_view->update();
    }
}

void Root_view::on_close()
{
    if (m_view != nullptr)
    {
        m_view->on_exit();
        m_view = nullptr;
    }

    m_last_view = nullptr;

    if (m_window != nullptr)
    {
        m_window->break_event_loop();
    }
}

void Root_view::on_resize(const int width, const int height)
{
    if (m_view != nullptr)
    {
        m_view->on_resize(width, height);
    }
}

void Root_view::on_key_press(const Keycode code, const Key_modifier_mask mask)
{
    if (m_view != nullptr)
    {
        m_view->on_key_press(code, mask);
    }
}

void Root_view::on_key_release(const Keycode code, const Key_modifier_mask mask)
{
    if (m_view != nullptr)
    {
        m_view->on_key_release(code, mask);
    }
}

void Root_view::on_mouse_move(const double x, const double y)
{
    if (m_view != nullptr)
    {
        m_view->on_mouse_move(x, y);
    }
}

void Root_view::on_mouse_click(const Mouse_button button, const int count)
{
    if (m_view != nullptr)
    {
        m_view->on_mouse_click(button, count);
    }
}

} // namespace erhe::toolkit
