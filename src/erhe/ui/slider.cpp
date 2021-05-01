#include "erhe/graphics_experimental/render_group.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/ui/ninepatch_style.hpp"
#include "erhe/ui/slider.hpp"
#include <sstream>

namespace erhe::ui
{

using erhe::toolkit::create_translation;
using glm::vec2;
using glm::vec4;
using glm::mat4;

auto Slider::label() const
-> const std::string&
{
    return m_label;
}

auto Slider::current_display_value() const
-> float
{
    return m_min_value + m_current_relative_value * (m_max_value - m_min_value);
}

auto Slider::slider_relative_value() const
-> float
{
    if (m_nonlinear)
    {
        return nonlinear_value();
    }

    return relative_value();
}

auto Slider::nonlinear_value() const
-> float
{
    return std::pow(relative_value(), 1.0f / 6.0f);
}

auto Slider::relative_value() const
-> float
{
    return m_current_relative_value;
}

auto Slider::min() const
-> float
{
    return m_min_value;
}

auto Slider::max() const
-> float
{
    return m_max_value;
}

void Slider::set_value(const std::string& value)
{
    m_label = value;
}

void Slider::set_current_display_value(float value)
{
    m_current_relative_value = (value - m_min_value) / (m_max_value - m_min_value);
}

void Slider::set_slider_relative_value(float value)
{
    if (m_nonlinear)
    {
        set_nonlinear_value(value);
    }
    else
    {
        set_relative_value(value);
    }
}

void Slider::set_nonlinear_value(float value)
{
    set_relative_value(std::pow(value, 6.0f));
}

void Slider::set_relative_value(float value)
{
    if (value != m_current_relative_value)
    {
        m_current_relative_value = value;
        m_value_dirty            = true;
    }
}

void Slider::set_min(float value)
{
    m_min_value = value;
}

void Slider::set_max(float value)
{
    m_max_value = value;
}

 Slider::Slider(Gui_renderer&               renderer,
                gsl::not_null<const Style*> style,
                const std::string&          label,
                float                       min,
                float                       max) noexcept
    : Area       {renderer, style}
    , m_label    {label}
    , m_min_value{min}
    , m_max_value{max}
{
    if (style->font != nullptr)
    {
        m_text_buffer = std::make_unique<Text_buffer>(renderer, style);
        renderer.render_group().make_draw(m_text_buffer->get_draw_key());
    }

    if (style->ninepatch_style != nullptr)
    {
        m_ninepatch = std::make_unique<Ninepatch>(renderer, style->ninepatch_style);
    }

    name = label;
    set_slider_relative_value(0.5f);
}

void Slider::begin_size(vec2 free_size_reference) noexcept
{
    update_size();
    Area::begin_size(free_size_reference);
}

void Slider::update_size()
{
    if (m_label_dirty)
    {
        if (m_text_buffer)
        {
            m_text_buffer->measure(m_label + ": 180.99");
            m_bounds = m_text_buffer->bounding_box();

            fill_base_pixels = m_bounds.max() + 2.0f * style->padding;
        }
        else
        {
            fill_base_pixels = vec2(30.0f, 10.0f);
        }

        if (m_ninepatch)
        {
            m_ninepatch->place(0.0f, 0.0f, fill_base_pixels.x, fill_base_pixels.y);
        }
        m_label_dirty = false;
        m_value_dirty = true;
    }
}

void Slider::begin_place(Rectangle reference, vec2 grow_direction) noexcept
{
    Area::begin_place(reference, grow_direction);
    update_place();
    auto text_offset       = create_translation(rect.min() + style->padding);
    auto background_offset = create_translation(rect.min());
    auto orthographic      = renderer.ortho();
    m_text_frame           = orthographic * text_offset;
    m_background_frame     = orthographic * background_offset;
}

void Slider::update_place()
{
    if (m_ninepatch)
    {
        if (size.x != m_bounds.max().x + 2.0f * style->padding.x)
        {
            m_ninepatch->place(0.0f, 0.0f, size.x, size.y);
        }
    }
}

void Slider::draw_self(ui_context& context) noexcept
{
    if (m_value_dirty && m_text_buffer)
    {
        std::stringstream ss;
        ss << m_label << ": " << current_display_value();
        m_text_buffer->begin_print();
        m_text_buffer->print(ss.str(), 0, 0);
        m_text_buffer->end_print();
    }

    auto& render_group = renderer.render_group();
    auto& u = renderer.current_uniform_state();

    u.clip_from_model = m_background_frame;
    u.color_scale = vec4(1.0f, 1.0f, 1.0f, 1.0f);

    if (rect.hit(context.mouse))
    {
        if (context.mouse_buttons[0])
        {
            float x              = context.mouse.x - rect.min().x;
            float relative_value = x / (rect.size().x - 1.0f);
            set_relative_value(relative_value);
            u.color_add = vec4(0.3f, 0.4f, 0.5f, 0.0f);
            set_trigger(true);
        }
        else
        {
            if (trigger())
            {
                auto* s = sink();
                if (s != nullptr)
                {
                    s->action(this);
                }

                set_trigger(false);
            }
            u.color_add = vec4(0.2f, 0.3f, 0.4f, 0.0f);
        }
    }
    else
    {
        set_trigger(false);
        u.color_add = vec4(0.1f, 0.2f, 0.3f, 0.0f);
    }

    float t       = relative_value();
    float pixel_x = rect.min().x + t * (rect.size().x - 1.0f);

    u.t = pixel_x;

    if (m_ninepatch)
    {
        m_ninepatch->update_render(renderer);
    }

    // Then draw text
    if (m_text_buffer)
    {
        // TODO(tksuoran@gmail.com): Font shader currently does not have color add
        // gr->set_color_add  (vec4(0.00f, 0.00f, 0.00f, 0.0f));
        u.color_scale = vec4(0.72f, 0.72f, 0.72f, 2.0f);
        u.clip_from_model = m_text_frame;

        render_group.update(*(m_text_buffer.get()));
    }
}

} // namespace erhe::ui
