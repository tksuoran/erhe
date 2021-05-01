#ifndef menulist_hpp_erhe_ui
#define menulist_hpp_erhe_ui

#include "erhe/ui/dock.hpp"
#include <memory>

namespace erhe::ui
{

class Ninepatch;

class Menulist
    : public Dock
{
public:
    Menulist(Gui_renderer&               renderer,
             gsl::not_null<const Style*> style,
             Orientation                 orientation,
             Flow_mode                   child_layout_style = Flow_mode::normal) noexcept;

    virtual ~Menulist() = default;

    void update();

    // Area methods

    void begin_place(Rectangle reference, glm::vec2 grow_direction) noexcept override;

    void draw_self(ui_context& context) noexcept override;

private:
    glm::mat4                  m_background_frame{1.0f};
    std::unique_ptr<Ninepatch> m_ninepatch;
};

} // namespace erhe::ui

#endif
