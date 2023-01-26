#pragma once

#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/toolkit/view.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <mutex>

namespace erhe::application {

class View_client
{
public:
    virtual void update() = 0;
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
{
public:
    static constexpr std::string_view c_type_name{"View"};
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
    void on_mouse_move  (float x, float y) override;
    void on_mouse_button(erhe::toolkit::Mouse_button button, bool pressed) override;
    void on_mouse_wheel (float x, float y) override;
    void on_key         (erhe::toolkit::Keycode code, uint32_t modifier_mask, bool pressed) override;
    void on_char        (unsigned int codepoint) override;

    // Public API
    void set_client(View_client* view_client);

    [[nodiscard]] auto shift_key_down       () const -> bool;
    [[nodiscard]] auto control_key_down     () const -> bool;
    [[nodiscard]] auto alt_key_down         () const -> bool;
    [[nodiscard]] auto mouse_button_pressed (erhe::toolkit::Mouse_button button) const -> bool;
    [[nodiscard]] auto mouse_button_released(erhe::toolkit::Mouse_button button) const -> bool;
    [[nodiscard]] auto mouse_position       () const -> glm::vec2;

    [[nodiscard]] auto view_client() const -> View_client*;

private:
    View_client* m_view_client    {nullptr};
    bool         m_ready          {false};
    bool         m_close_requested{false};

    class Mouse_button
    {
    public:
        bool pressed {false};
        bool released{false};
    };

    bool      m_mouse_button[static_cast<int>(erhe::toolkit::Mouse_button_count)]{};
    glm::vec2 m_mouse_position{0.0f, 0.0f};
    bool      m_shift  {false};
    bool      m_control{false};
    bool      m_alt    {false};
};

extern View* g_view;

} // namespace erhe::application
