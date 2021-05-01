#ifndef dock_hpp_erhe_ui
#define dock_hpp_erhe_ui

#include "erhe/ui/area.hpp"

namespace erhe::ui
{

class Dock
    : public Area
{
public:
    Dock(Gui_renderer&               renderer,
         gsl::not_null<const Style*> style,
         Orientation                 orientation,
         Flow_mode                   child_layout_style = Flow_mode::normal) noexcept;

    virtual ~Dock() = default;

    auto orientation() const
    -> Orientation
    {
        return m_orientation;
    }

    void set_orientation(Orientation value)
    {
        m_orientation = value;
    }

    auto child_layout_style() const
    -> Area::Flow_mode
    {
        return m_child_layout_style;
    }

    void set_child_layout_style(Area::Flow_mode value)
    {
        m_child_layout_style = value;
    }

    // Area methods

    void begin_size(glm::vec2 free_size_reference) noexcept override;

    void call_size(gsl::not_null<Area*> area) noexcept override;

    auto add(gsl::not_null<Area*> area) noexcept
    -> gsl::not_null<Area*> override;

    void end_size() noexcept override;

    void begin_place(Rectangle reference, glm::vec2 container_grow_direction) noexcept override;

    void call_place(gsl::not_null<Area*> area) noexcept override;

private:
    Orientation     m_orientation       {Orientation::horizontal};
    glm::vec2       m_cursor_start      {0.0f};
    glm::vec2       m_cursor_end        {0.0f};
    glm::vec2       m_grow_direction    {0.0f};
    Area::Flow_mode m_child_layout_style{Flow_mode::normal};
};

} // namespace erhe::ui

#endif
