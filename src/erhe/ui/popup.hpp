#ifndef popup_hpp_erhe_ui
#define popup_hpp_erhe_ui

#include "erhe/ui/dock.hpp"

namespace erhe::ui
{

class Popup
    : public Dock
{
public:
    Popup(Gui_renderer& renderer,
          Area*         closed,
          Area*         open) noexcept;

    virtual ~Popup() = default;

    auto current() const
    -> Area*
    {
        return (m_is_open ? m_open : m_closed);
    }

    auto closed() const
    -> Area*
    {
        return m_closed;
    }

    void set_closed(Area* value);

    auto open() const
    -> Area*
    {
        return m_open;
    }

    void set_open(Area* value);

    auto is_open() const
    -> bool
    {
        return m_is_open;
    }

    void toggle();

    // Set(true) to open
    // Set(false) to close
    void set(bool open);

private:
    Area* m_closed {nullptr};
    Area* m_open   {nullptr};
    bool  m_is_open{true};
};

} // namespace erhe::ui

#endif
