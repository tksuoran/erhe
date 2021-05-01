#ifndef push_button_hpp_erhe_ui
#define push_button_hpp_erhe_ui

#include "erhe/ui/button.hpp"

namespace erhe::ui
{

class Push_button
    : public Button
{
public:
    Push_button(Gui_renderer&               renderer,
                std::string                 label,
                gsl::not_null<const Style*> style);

    virtual ~Push_button() = default;

    bool pressed() const
    {
        return m_pressed;
    }

    void set_pressed(bool value)
    {
        m_pressed = value;
    }

    // Area methods

    void draw_self(ui_context& context) noexcept override;

private:
    bool                                       m_pressed{false};

    erhe::graphics::Render_group::draw_index_t m_draw{};
};

} // namespace erhe::ui

#endif
