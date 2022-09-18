#pragma once

#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/toolkit/view.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <mutex>

namespace erhe::application {

class Commands;
class Configuration;
class Imgui_renderer;
class Imgui_windows;
class Rendergraph;
class Time;
class View;
class Window;

class View_client
{
public:
    virtual void update() = 0;
    virtual void update_keyboard(
        const bool                   pressed,
        const erhe::toolkit::Keycode code,
        const uint32_t               modifier_mask
    ) = 0;
    virtual void update_mouse(const erhe::toolkit::Mouse_button button, const int count) = 0;
    virtual void update_mouse(const double x, const double y) = 0;
};

class Render_task
{
public:
    virtual void execute() = 0;

    std::vector<Render_task*> dependencies;
};

class View
    : public erhe::components::Component
    , public erhe::toolkit::View
    , public Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"View"};
    static constexpr std::string_view c_title    {"View"};
    static constexpr uint32_t         c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    View ();
    ~View() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    void run();
    auto get_imgui_capture_keyboard() const -> bool;
    auto get_imgui_capture_mouse   () const -> bool;

    // Implements erhe::toolkit::View
    void update         () override;
    void on_close       () override;
    void on_focus       (int focused) override;
    void on_cursor_enter(int entered) override;
    void on_refresh     () override;
    void on_enter       () override;
    void on_mouse_move  (const double x, const double y) override;
    void on_mouse_click (const erhe::toolkit::Mouse_button button, const int count) override;
    void on_mouse_wheel (const double x, const double y) override;
    void on_key         (const erhe::toolkit::Keycode code, const uint32_t modifier_mask, const bool pressed) override;
    void on_char        (const unsigned int codepoint) override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void set_client(View_client* view_client);

    [[nodiscard]] auto view_client() const -> View_client*;

private:
    // Component dependencies
    std::shared_ptr<Commands>       m_commands;
    std::shared_ptr<Configuration>  m_configuration;
    std::shared_ptr<Imgui_renderer> m_imgui_renderer;
    std::shared_ptr<Imgui_windows>  m_imgui_windows;
    std::shared_ptr<Rendergraph>    m_render_graph;
    std::shared_ptr<Time>           m_time;
    std::shared_ptr<Window>         m_window;

    View_client* m_view_client    {nullptr};
    bool         m_ready          {false};
    bool         m_close_requested{false};
};

} // namespace erhe::application
