#ifndef slider_hpp_erhe_ui
#define slider_hpp_erhe_ui

#include "erhe/ui/action.hpp"
#include "erhe/ui/area.hpp"
#include "erhe/ui/gui_renderer.hpp"
#include "erhe/ui/ninepatch.hpp"
#include "erhe/ui/text_buffer.hpp"

namespace erhe::ui
{

class Slider
    : public Area
    , public Action_source
{
public:
    explicit Slider(Gui_renderer&               renderer,
                    gsl::not_null<const Style*> style,
                    const std::string&          label,
                    float                       min,
                    float                       max) noexcept;

    virtual ~Slider() = default;

    auto label() const
    -> const std::string&;

    auto current_display_value() const
    -> float;

    auto slider_relative_value() const
    -> float;

    auto nonlinear_value() const
    -> float;

    auto relative_value() const
    -> float;

    auto min() const
    -> float;

    auto max() const
    -> float;

    auto trigger() const
    -> bool
    {
        return m_trigger;
    }

    void set_value(const std::string& value);

    void set_current_display_value(float value);

    void set_slider_relative_value(float value);

    void set_nonlinear_value(float value);

    void set_relative_value(float value);

    void set_min(float value);

    void set_max(float value);

    void set_trigger(bool value)
    {
        m_trigger = value;
    }

    void update_size();

    void update_place();

    // Area methods

    void begin_size(glm::vec2 free_size_reference) noexcept override;

    void begin_place(Rectangle reference, glm::vec2 grow_direction) noexcept override;

    void draw_self(ui_context& context) noexcept override;

private:
    glm::mat4                    m_text_frame{1.0f};
    glm::mat4                    m_background_frame{1.0f};
    std::unique_ptr<Text_buffer> m_text_buffer;
    std::unique_ptr<Ninepatch>   m_ninepatch;
    Rectangle                    m_bounds;
    std::string                  m_label;
    float                        m_min_value{0.0f};
    float                        m_max_value{0.0f};
    bool                         m_label_dirty{true};
    bool                         m_value_dirty{true};
    bool                         m_nonlinear{true};
    bool                         m_trigger{false};
    float                        m_current_relative_value{0.0f};
};

} // namespace erhe::ui

#endif
