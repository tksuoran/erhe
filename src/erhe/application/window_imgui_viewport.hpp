#pragma once

#include "erhe/application/imgui_viewport.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/viewport.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <gsl/gsl>

struct ImFontAtlas;

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Texture;
}

namespace erhe::scene
{
    class Mesh;
}

namespace erhe::application
{

class View;
class Imgui_window;
class Imgui_renderer;

class Window_imgui_viewport
    : public Imgui_viewport

{
public:
    Window_imgui_viewport(
        const std::shared_ptr<Imgui_renderer>& imgui_renderer,
        const std::shared_ptr<Window>&         window
    );
    ~Window_imgui_viewport() noexcept override;

    void render_imgui_frame();

    void make_imgui_context_current  ();
    void make_imgui_context_uncurrent();

    // Implements Imgui_vewport
    void begin_imgui_frame(View& view) override;
    void end_imgui_frame  ()           override;

    [[nodiscard]] auto want_capture_mouse() const -> bool override;

    void on_key         (const signed int keycode, const uint32_t modifier_mask, const bool pressed) override;
    void on_char        (const unsigned int codepoint)                                               override;
    void on_focus       (int focused)                                                                override;
    void on_cursor_enter(int entered)                                                                override;
    void on_mouse_click (const uint32_t button, const int count)                                     override;
    void on_mouse_wheel (const double x, const double y)                                             override;

private:
    void menu        ();
    //void window_menu       ();

    std::shared_ptr<Imgui_renderer> m_imgui_renderer;
    std::shared_ptr<Window>         m_window;

    double                  m_time         {0.0};
    bool                    m_has_cursor   {false};

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    bool                    m_mouse_just_pressed[ImGuiMouseButton_COUNT];
    ImGuiContext*           m_imgui_context{nullptr};
    ImVector<ImWchar>       m_glyph_ranges;
#endif
};

} // namespace erhe::application
