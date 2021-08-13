#include "erhe/ui/button.hpp"

#include "erhe/graphics_experimental/render_group.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/ui/gui_renderer.hpp"
#include "erhe/ui/log.hpp"
#include "erhe/ui/ninepatch.hpp"
#include "erhe/ui/ninepatch_style.hpp"
#include "erhe/ui/text_buffer.hpp"

#include <gsl/assert>

namespace erhe::ui
{

using erhe::log::Log;
using glm::vec2;
using glm::vec4;
using glm::mat4;

Button::Button(Gui_renderer&               renderer,
               const std::string&          label,
               gsl::not_null<const Style*> style)
    : Area{renderer, style}
{
    name = label;
    m_label = label;

    if (style->ninepatch_style != nullptr)
    {
        m_ninepatch = std::make_unique<Ninepatch>(renderer, style->ninepatch_style);
    }

    if (style->font != nullptr)
    {
        m_text_buffer = std::make_unique<Text_buffer>(renderer, style);
    }
}

auto Button::label() const
-> const std::string&
{
    return m_label;
}

void Button::set_label(std::string value)
{
    if (value != m_label)
    {
        m_dirty = true;
        m_label = std::move(value);
    }
}

auto Button::text_frame()
-> mat4&
{
    return m_text_frame;
}

auto Button::background_frame()
-> mat4&
{
    return m_background_frame;
}

auto Button::ninepatch()
-> Ninepatch*
{
    return m_ninepatch.get();
}

auto Button::trigger() const
-> bool
{
    return m_trigger;
}

void Button::set_trigger(bool value)
{
    m_trigger = value;
}

auto Button::text_buffer()
-> Text_buffer*
{
    return m_text_buffer.get();
}

void Button::update_size()
{
    log_button.trace("Button::update_size()");

    if (m_dirty)
    {
        if (m_text_buffer)
        {
            m_text_buffer->begin_print();
            m_text_buffer->print(0, 0, m_label);
            m_text_buffer->end_print();
            m_bounds = m_text_buffer->bounding_box();

            fill_base_pixels = m_bounds.max() + 2.0f * style->padding;
        }
        else
        {
            fill_base_pixels = glm::vec2(30.0f, 10.0f);
        }

        if (m_ninepatch)
        {
            m_ninepatch->place(0.0f, 0.0f, fill_base_pixels.x, fill_base_pixels.y);
        }
        m_dirty = false;
    }
}

void Button::update_place()
{
    log_button.trace("Button::update_place()");
    Log::Indenter indenter;

    if (m_ninepatch)
    {
        if (size.x != m_bounds.max().x + 2.0f * style->padding.x)
        {
            m_ninepatch->place(0.0f, 0.0f, size.x, size.y);
        }
    }
}

void Button::begin_size(glm::vec2 free_size_reference) noexcept
{
    update_size();
    Area::begin_size(free_size_reference);
}

void Button::begin_place(Rectangle reference, glm::vec2 grow_direction) noexcept
{
    Area::begin_place(reference, grow_direction);
    update_place();
    auto text_offset       = erhe::toolkit::create_translation(rect.min() + style->padding);
    auto background_offset = erhe::toolkit::create_translation(rect.min());
    auto orthographic      = renderer.ortho();
    m_text_frame           = orthographic * text_offset;
    m_background_frame     = orthographic * background_offset;
}

void Button::draw_self(ui_context &context) noexcept
{
    log_button.trace("Button::draw_self()");
    Log::Indenter indenter;

    auto& u = renderer.current_uniform_state();

    u.clip_from_model = m_background_frame;
    u.color_add = vec4(1.0f, 1.0f, 1.0f, 1.0f);

    if (rect.hit(context.mouse))
    {
        if (context.mouse_buttons[0])
        {
            u.color_add = glm::vec4(0.2f, 0.35f, 0.45f, 0.0f);
            m_trigger = true;
        }
        else
        {
            if (trigger())
            {
                if (sink() != nullptr)
                {
                    sink()->action(this);
                }
                else
                {
                    m_pressed = !m_pressed;
                }
                set_trigger(false);
            }
            u.color_add = m_pressed ? vec4(0.0f, 0.0f, 1.0f, 0.0f)
                                    : vec4(0.72f, 0.72f, 0.72f, 0.0f);
        }
    }
    else
    {
        set_trigger(false);
        u.color_add = m_pressed ? vec4(0.0f, 0.0f, 1.0f, 0.0f)
                                : vec4(0.72f, 0.72f, 0.72f, 0.0f);
    }

    if (m_ninepatch)
    {
        m_ninepatch->update_render(renderer);
    }

#if 0
    // Then draw text
    if (m_text_buffer)
    {
        u.color_add       = glm::vec4(0.00f, 0.00f, 0.00f, 0.0f);
        u.color_scale     = glm::vec4(0.72f, 0.72f, 0.72f, 2.0f);
        u.clip_from_model = m_text_frame;

        auto draw_index = m_text_buffer->draw_index();
        renderer.record_uniforms(draw_index);
        render_group.update_draw(draw_index, m_text_buffer->draw());
    }
#endif
}

} // namespace erhe::ui
