#pragma once

#include "erhe/application/view.hpp"
#include "erhe/components/components.hpp"

namespace editor
{

class Editor_rendering;
class Pointer_context;

class View_client
    : public erhe::application::View_client
    , public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"hextiles::View_client"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    View_client ();
    ~View_client () noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements erhe::application::View_client
    void initial_state           () override;
    void update                  () override;
    void render                  () override;
    void bind_default_framebuffer() override;
    void clear                   () override;
    void update_imgui_window(erhe::application::Imgui_window* imgui_window) override;
    void update_keyboard(
        const bool                   pressed,
        const erhe::toolkit::Keycode code,
        const uint32_t               modifier_mask
    ) override;
    void update_mouse(const erhe::toolkit::Mouse_button button, const int count) override;
    void update_mouse(const double x, const double y) override;

private:
    std::shared_ptr<erhe::application::Imgui_windows> m_imgui_windows;
    std::shared_ptr<Editor_rendering                > m_editor_rendering;
    std::shared_ptr<Pointer_context                 > m_pointer_context;
};

} // namespace editor

