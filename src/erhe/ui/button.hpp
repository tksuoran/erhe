#ifndef button_hpp_erhe_ui
#define button_hpp_erhe_ui

#include "erhe/ui/action.hpp"
#include "erhe/ui/area.hpp"
#include "erhe/ui/ninepatch.hpp"
#include "erhe/ui/text_buffer.hpp"

namespace erhe::ui
{

class Ninepatch;
class Text_buffer;

class Button
    : public Area
    , public Action_source
{
public:
    Button(Gui_renderer&               renderer,
           const std::string&          label,
           gsl::not_null<const Style*> style);

    virtual ~Button() = default;

    auto label() const
    -> const std::string&;

    void set_label(std::string value);

    auto pressed() const
    -> bool
    {
        return m_pressed;
    }

    void set_pressed(bool value)
    {
        m_pressed = value;
    }

    void update_size();

    void update_place();

    // Area methods

    void begin_size(glm::vec2 free_size_reference) noexcept override;

    void begin_place(Rectangle reference, glm::vec2 grow_direction) noexcept override;

    void draw_self(ui_context& context) noexcept override;

protected:
    auto text_frame()
    -> glm::mat4&;

    auto background_frame()
    -> glm::mat4&;

    auto ninepatch()
    -> Ninepatch*;

    auto trigger() const
    -> bool;

    void set_trigger(bool value);

    auto text_buffer()
    -> Text_buffer*;

private:
    glm::mat4                    m_text_frame      {1.0f};
    glm::mat4                    m_background_frame{1.0f};
    std::unique_ptr<Text_buffer> m_text_buffer;
    std::unique_ptr<Ninepatch>   m_ninepatch;
    Rectangle                    m_bounds;
    std::string                  m_label;
    bool                         m_dirty   {true};
    bool                         m_trigger {false};
    bool                         m_pressed {false};
};

} // namespace erhe::ui

#endif
