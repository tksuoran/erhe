#include "erhe/ui/menulist.hpp"

#include "erhe/graphics_experimental/render_group.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/ui/gui_renderer.hpp"
#include "erhe/ui/ninepatch.hpp"
#include "erhe/ui/ninepatch_style.hpp"

namespace erhe::ui
{

using erhe::toolkit::create_translation;
using glm::vec2;
using glm::vec4;
using glm::mat4;

Menulist::Menulist(Gui_renderer&               renderer,
                   gsl::not_null<const Style*> style,
                   Orientation                 orientation,
                   Flow_mode                   child_layout_style) noexcept
    : Dock{renderer, style, orientation, child_layout_style}
{
    if (style->ninepatch_style != nullptr)
    {
        m_ninepatch = std::make_unique<Ninepatch>(renderer, style->ninepatch_style);
    }
}

void Menulist::update()
{
    if (m_ninepatch)
    {
        if (m_ninepatch->size() != rect.size())
        {
            m_ninepatch->place(0.0f, 0.0f, rect.size().x, rect.size().y);
        }
    }
}

void Menulist::begin_place(Rectangle reference, glm::vec2 grow_direction) noexcept
{
    Dock::begin_place(reference, grow_direction);
    auto a = create_translation(rect.min());
    auto o = renderer.ortho();
    m_background_frame = o * a;
}

void Menulist::draw_self(ui_context &context) noexcept
{
    update();

    if (!m_ninepatch)
    {
        return;
    }

    auto& u = renderer.current_uniform_state();

    u.clip_from_model = m_background_frame;
    if (rect.hit(context.mouse))
    {
        u.color_scale = glm::vec4( 1.00f,  1.00f, 1.0f, 0.125f);
        u.color_add   = glm::vec4(-0.25f, -0.25f, 0.1f, 0.000f);
    }
    else
    {
        u.color_scale = glm::vec4(1.0f, 1.0f, 1.0f, 0.125f);
        u.color_add   = glm::vec4(0.0f, 0.0f, 0.0f, 0.000f);
    }
    m_ninepatch->update_render(renderer);
}

} // namespace erhe::ui

