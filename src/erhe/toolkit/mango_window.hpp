#ifndef mango_window_hpp_erhe_toolkit
#define mango_window_hpp_erhe_toolkit

#if defined(ERHE_WINDOW_LIBRARY_MANGO)

#include "erhe/toolkit/view.hpp"
#include "erhe/gl/dynamic_load.hpp"
#include "erhe/log/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include <mango/window/window.hpp>
#include <mango/opengl/opengl.hpp>

#include <string>


namespace erhe::toolkit
{

typedef void (*glproc)(void);

namespace {

HMODULE s_module = nullptr;

glproc getProcAddress(const char* name)
{
    auto p = reinterpret_cast<glproc>(wglGetProcAddress(name));
    if ((p == nullptr) ||
        (p == (void*)(0x1u)) ||
        (p == (void*)(0x2u)) ||
        (p == (void*)(0x3u)) ||
        (p == (void*)(-1  )))
    {
        if (s_module == nullptr)
        {
            s_module = LoadLibraryA("opengl32.dll");
        }
        if (s_module != nullptr)
        {
            p = reinterpret_cast<glproc>(GetProcAddress(s_module, name));
        }
    }
    return p;
}

Keycode mango_keycode_to_erhe(mango::Keycode mango_keycode)
{
    switch (mango_keycode)
    {
        //using enum mango::Keycode;
        case mango::Keycode::KEYCODE_NONE:         return Keycode::KEY_UNKNOWN;
        case mango::Keycode::KEYCODE_ESC:          return Keycode::KEY_ESCAPE;
        case mango::Keycode::KEYCODE_0:            return Keycode::KEY_0;
        case mango::Keycode::KEYCODE_1:            return Keycode::KEY_1;
        case mango::Keycode::KEYCODE_2:            return Keycode::KEY_2;
        case mango::Keycode::KEYCODE_3:            return Keycode::KEY_3;
        case mango::Keycode::KEYCODE_4:            return Keycode::KEY_4;
        case mango::Keycode::KEYCODE_5:            return Keycode::KEY_5;
        case mango::Keycode::KEYCODE_6:            return Keycode::KEY_6;
        case mango::Keycode::KEYCODE_7:            return Keycode::KEY_7;
        case mango::Keycode::KEYCODE_8:            return Keycode::KEY_8;
        case mango::Keycode::KEYCODE_9:            return Keycode::KEY_9;
        case mango::Keycode::KEYCODE_A:            return Keycode::KEY_A;
        case mango::Keycode::KEYCODE_B:            return Keycode::KEY_B;
        case mango::Keycode::KEYCODE_C:            return Keycode::KEY_C;
        case mango::Keycode::KEYCODE_D:            return Keycode::KEY_D;
        case mango::Keycode::KEYCODE_E:            return Keycode::KEY_E;
        case mango::Keycode::KEYCODE_F:            return Keycode::KEY_F;
        case mango::Keycode::KEYCODE_G:            return Keycode::KEY_G;
        case mango::Keycode::KEYCODE_H:            return Keycode::KEY_H;
        case mango::Keycode::KEYCODE_I:            return Keycode::KEY_I;
        case mango::Keycode::KEYCODE_J:            return Keycode::KEY_J;
        case mango::Keycode::KEYCODE_K:            return Keycode::KEY_K;
        case mango::Keycode::KEYCODE_L:            return Keycode::KEY_L;
        case mango::Keycode::KEYCODE_M:            return Keycode::KEY_M;
        case mango::Keycode::KEYCODE_N:            return Keycode::KEY_N;
        case mango::Keycode::KEYCODE_O:            return Keycode::KEY_O;
        case mango::Keycode::KEYCODE_P:            return Keycode::KEY_P;
        case mango::Keycode::KEYCODE_Q:            return Keycode::KEY_Q;
        case mango::Keycode::KEYCODE_R:            return Keycode::KEY_R;
        case mango::Keycode::KEYCODE_S:            return Keycode::KEY_S;
        case mango::Keycode::KEYCODE_T:            return Keycode::KEY_T;
        case mango::Keycode::KEYCODE_U:            return Keycode::KEY_U;
        case mango::Keycode::KEYCODE_V:            return Keycode::KEY_V;
        case mango::Keycode::KEYCODE_W:            return Keycode::KEY_W;
        case mango::Keycode::KEYCODE_X:            return Keycode::KEY_X;
        case mango::Keycode::KEYCODE_Y:            return Keycode::KEY_Y;
        case mango::Keycode::KEYCODE_Z:            return Keycode::KEY_Z;
        case mango::Keycode::KEYCODE_F1:           return Keycode::KEY_F1;
        case mango::Keycode::KEYCODE_F2:           return Keycode::KEY_F2;
        case mango::Keycode::KEYCODE_F3:           return Keycode::KEY_F3;
        case mango::Keycode::KEYCODE_F4:           return Keycode::KEY_F4;
        case mango::Keycode::KEYCODE_F5:           return Keycode::KEY_F5;
        case mango::Keycode::KEYCODE_F6:           return Keycode::KEY_F6;
        case mango::Keycode::KEYCODE_F7:           return Keycode::KEY_F7;
        case mango::Keycode::KEYCODE_F8:           return Keycode::KEY_F8;
        case mango::Keycode::KEYCODE_F9:           return Keycode::KEY_F9;
        case mango::Keycode::KEYCODE_F10:          return Keycode::KEY_F10;
        case mango::Keycode::KEYCODE_F11:          return Keycode::KEY_F11;
        case mango::Keycode::KEYCODE_F12:          return Keycode::KEY_F12;
        case mango::Keycode::KEYCODE_BACKSPACE:    return Keycode::KEY_BACKSPACE;
        case mango::Keycode::KEYCODE_TAB:          return Keycode::KEY_TAB;
        case mango::Keycode::KEYCODE_RETURN:       return Keycode::KEY_ENTER;
        case mango::Keycode::KEYCODE_LEFT_ALT:     return Keycode::KEY_LEFT_ALT;
        case mango::Keycode::KEYCODE_RIGHT_ALT:    return Keycode::KEY_RIGHT_ALT;
        case mango::Keycode::KEYCODE_SPACE:        return Keycode::KEY_SPACE;
        case mango::Keycode::KEYCODE_CAPS_LOCK:    return Keycode::KEY_CAPS_LOCK;
        case mango::Keycode::KEYCODE_PAGE_UP:      return Keycode::KEY_PAGE_UP;
        case mango::Keycode::KEYCODE_PAGE_DOWN:    return Keycode::KEY_PAGE_DOWN;
        case mango::Keycode::KEYCODE_INSERT:       return Keycode::KEY_INSERT;
        case mango::Keycode::KEYCODE_DELETE:       return Keycode::KEY_DELETE;
        case mango::Keycode::KEYCODE_HOME:         return Keycode::KEY_HOME;
        case mango::Keycode::KEYCODE_END:          return Keycode::KEY_END;
        case mango::Keycode::KEYCODE_LEFT:         return Keycode::KEY_LEFT;
        case mango::Keycode::KEYCODE_RIGHT:        return Keycode::KEY_RIGHT;
        case mango::Keycode::KEYCODE_UP:           return Keycode::KEY_UP;
        case mango::Keycode::KEYCODE_DOWN:         return Keycode::KEY_DOWN;
        case mango::Keycode::KEYCODE_PRINT_SCREEN: return Keycode::KEY_PRINT_SCREEN;
        case mango::Keycode::KEYCODE_SCROLL_LOCK:  return Keycode::KEY_SCROLL_LOCK;
        case mango::Keycode::KEYCODE_NUMPAD0:      return Keycode::KEY_KP_0;
        case mango::Keycode::KEYCODE_NUMPAD1:      return Keycode::KEY_KP_1;
        case mango::Keycode::KEYCODE_NUMPAD2:      return Keycode::KEY_KP_2;
        case mango::Keycode::KEYCODE_NUMPAD3:      return Keycode::KEY_KP_3;
        case mango::Keycode::KEYCODE_NUMPAD4:      return Keycode::KEY_KP_4;
        case mango::Keycode::KEYCODE_NUMPAD5:      return Keycode::KEY_KP_5;
        case mango::Keycode::KEYCODE_NUMPAD6:      return Keycode::KEY_KP_6;
        case mango::Keycode::KEYCODE_NUMPAD7:      return Keycode::KEY_KP_7;
        case mango::Keycode::KEYCODE_NUMPAD8:      return Keycode::KEY_KP_8;
        case mango::Keycode::KEYCODE_NUMPAD9:      return Keycode::KEY_KP_9;
        case mango::Keycode::KEYCODE_NUMLOCK:      return Keycode::KEY_NUM_LOCK;
        case mango::Keycode::KEYCODE_DIVIDE:       return Keycode::KEY_KP_DIVIDE;
        case mango::Keycode::KEYCODE_MULTIPLY:     return Keycode::KEY_KP_MULTIPLY;
        case mango::Keycode::KEYCODE_SUBTRACT:     return Keycode::KEY_KP_SUBTRACT;
        case mango::Keycode::KEYCODE_ADDITION:     return Keycode::KEY_KP_ADD;
        case mango::Keycode::KEYCODE_ENTER:        return Keycode::KEY_KP_ENTER;
        case mango::Keycode::KEYCODE_DECIMAL:      return Keycode::KEY_KP_DECIMAL;
        default: return Keycode::KEY_UNKNOWN;
    }
}

Mouse_button mango_mouse_button_to_erhe(mango::MouseButton mango_mouse_button)
{
    switch (mango_mouse_button)
    {
        //using enum mango::MouseButton;
        case mango::MouseButton::MOUSEBUTTON_LEFT:   return Mouse_button::MOUSE_BUTTON_LEFT;
        case mango::MouseButton::MOUSEBUTTON_RIGHT:  return Mouse_button::MOUSE_BUTTON_RIGHT;
        case mango::MouseButton::MOUSEBUTTON_MIDDLE: return Mouse_button::MOUSE_BUTTON_MIDDLE;
        case mango::MouseButton::MOUSEBUTTON_X1:     return Mouse_button::MOUSE_BUTTON_X1;
        case mango::MouseButton::MOUSEBUTTON_X2:     return Mouse_button::MOUSE_BUTTON_X2;
        case mango::MouseButton::MOUSEBUTTON_WHEEL:  return Mouse_button::MOUSE_BUTTON_WHEEL;
        default: return MOUSE_BUTTON_LEFT;
    }
}

} // anonymous namespace

class MangoContext
    : public mango::OpenGLContext
{
public:
    MangoContext(Event_handler* event_handler, int width, int height)
        : mango::OpenGLContext(width, height)
        , m_event_handler(event_handler)
    {
    }

    virtual ~MangoContext() = default;

    void onClose() override
    {
        m_event_handler->on_close();
    }

    void onResize(int width, int height) override
    {
        m_event_handler->on_resize(width, height);
    }

    void onIdle() override
    {
        m_event_handler->on_idle();
    }

    void onKeyPress(mango::Keycode code, mango::u32 mask) override
    {
        m_event_handler->on_key_press(mango_keycode_to_erhe(code), static_cast<uint32_t>(mask));
    }

    void onKeyRelease(mango::Keycode code) override
    {
        m_event_handler->on_key_release(mango_keycode_to_erhe(code));
    }

    void onMouseMove(int x, int y) override
    {
        m_event_handler->on_mouse_move(static_cast<double>(x), static_cast<double>(y));
    }

    void onMouseClick(int x, int y, mango::MouseButton button, int count) override
    {
        m_event_handler->on_mouse_move(x, y);
        m_event_handler->on_mouse_click(mango_mouse_button_to_erhe(button), count);
    }

private:
    Event_handler* m_event_handler{nullptr};
};


class Context_window
{
public:
    explicit Context_window(Context_window* share)
        : m_root_view{this}
    {
        VERIFY(share != nullptr);

        bool ok = open(64, 64, "", share->get_opengl_major_version(), share->get_opengl_minor_version(), share);

        VERIFY(ok);
    }

    Context_window(int width, int height)
        : m_root_view{this}
    {
        bool ok = open(width, height, "", 4, 6, nullptr);
        VERIFY(ok);
    }

    ~Context_window() = default;

    auto open(int width, int height, const std::string& title, int opengl_major_version, int opengl_minor_version, Context_window* share)
    -> bool
    {
        VERIFY(share == nullptr); // TODO Implement share contexts in mango
        m_mango_context = std::make_unique<MangoContext>(&m_root_view, width, height);
        gl::dynamic_load_init(getProcAddress);

        return true;
    }

    void make_current() const
    {
        m_mango_context->makeCurrent();
    }

    void clear_current() const
    {
        // TODO implement in mango
        FATAL("clear_current() not implemented in mango");
    }

    void swap_buffers() const
    {
        if (!static_cast<bool>(m_mango_context))
        {
            return;
        }
        m_mango_context->swapBuffers();
    }

    void break_event_loop()
    {
        if (!static_cast<bool>(m_mango_context))
        {
            return;
        }
        m_mango_context->breakEventLoop();
    }

    void enter_event_loop()
    {
        if (!static_cast<bool>(m_mango_context))
        {
            return;
        }
        m_mango_context->enterEventLoop();
    }

    auto get_width()
    -> int
    {
        if (!static_cast<bool>(m_mango_context))
        {
            return 0;
        }

        return m_mango_context->getWindowSize().x;
    }

    auto get_height()
    -> int
    {
        if (!static_cast<bool>(m_mango_context))
        {
            return 0;
        }

        return m_mango_context->getWindowSize().y;
    }

    auto get_root_view()
    -> Root_view&
    {
        return m_root_view;
    }

    auto get_opengl_major_version() const
    -> int
    {
        int version = m_mango_context->getVersion();
        int major_version = version / 100;
        //int minor_version = (version - (major_version * 100)) / 10;
        return major_version;
    }

    auto get_opengl_minor_version() const
    -> int
    {
        int version = m_mango_context->getVersion();
        int major_version = version / 100;
        int minor_version = (version - (major_version * 100)) / 10;
        return minor_version;
    }

    void get_cursor_position(double &xpos, double &ypos);

    void set_visible(bool visible)
    {
        m_mango_context->setVisible(visible);
    }

    void show_cursor(bool show);

    void capture_mouse(bool capture);

    auto is_mouse_captured() const;

private:
    void get_extensions();

    Root_view                     m_root_view;
    std::unique_ptr<MangoContext> m_mango_context;
    bool                          m_is_event_loop_running{false};
    bool                          m_is_mouse_captured{false};
};

} // namespace erhe::toolkit

#endif // defined(ERHE_WINDOW_LIBRARY_MANGO)

#endif
