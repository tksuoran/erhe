#include "erhe/ui/area.hpp"
#include "erhe/ui/gui_renderer.hpp"
#include "erhe/ui/log.hpp"
#include <glm/gtx/string_cast.hpp>

namespace erhe::ui
{

using erhe::log::Log;
using glm::vec2;

Area::Area(Gui_renderer& renderer) noexcept
    : Area{renderer, renderer.default_style()}
{
}

Area::Area(Gui_renderer&               renderer,
           gsl::not_null<const Style*> style) noexcept
    : renderer{renderer}
    , style   {style}
{

}

Area::Area(Gui_renderer&               renderer,
           gsl::not_null<const Style*> style,
           Flow_direction              layout_x_order,
           Flow_direction              layout_y_order) noexcept
    : renderer      {renderer}
    , style         {style}
    , layout_x_order(layout_x_order)
    , layout_y_order(layout_y_order)
{
}

auto Area::add(gsl::not_null<Area*> area)
-> gsl::not_null<Area*>
{
    if (m_in_draw)
    {
        m_add_list.push_back(area);
    }
    else
    {
        children.push_back(area);
        area->parent = this;
    }
    return area;
}

auto Area::remove(gsl::not_null<Area*> area)
-> gsl::not_null<Area*>
{
    if (m_in_draw)
    {
        m_remove_list.push_back(area);
    }
    else
    {
        children.erase(std::remove(children.begin(), children.end(), area), children.end());
        area->parent = nullptr;
    }
    return area;
}

auto Area::get_hit(glm::vec2 hit_position) noexcept
-> Area*
{
    if (hidden)
    {
        return nullptr;
    }

    if (event_ordering == Order::self_first)
    {
        if (rect.hit(hit_position))
        {
            return this;
        }
    }

    for (auto* child : children)
    {
        auto* hit = child->get_hit(hit_position);
        if (hit != nullptr)
        {
            return hit;
        }
    }

    if (event_ordering == Order::post_self)
    {
        if (rect.hit(hit_position))
        {
            return this;
        }
    }

    return nullptr;
}

void Area::draw_self(ui_context& context) noexcept
{
    static_cast<void>(context);
}

void Area::draw(ui_context& context) noexcept
{
    if (hidden)
    {
        return;
    }

    m_in_draw = true;
    if (draw_ordering == Order::self_first)
    {
        draw_self(context);
    }

    for (auto* child : children)
    {
        child->draw(context);
    }

    if (draw_ordering == Order::post_self)
    {
        draw_self(context);
    }

    m_in_draw = false;

    for (auto* i : m_add_list)
    {
        add(i);
    }

    for (auto* i : m_remove_list)
    {
        remove(i);
    }

    m_add_list.clear();
    m_remove_list.clear();
}

// Layout

void Area::begin_size(glm::vec2 free_size_reference) noexcept
{
    log_layout.trace("{} Area::begin_size() reference = {}, {}\n",
                     name, free_size_reference.x, free_size_reference.y);
    Log::Indenter indenter;

    size = fill_base_pixels + free_size_reference * fill_free_size_relative;
    size = glm::round(size);

    log_layout.trace("{} size = {}, {}\n", name, size.x, size.y);
}

void Area::call_size(gsl::not_null<Area *> area) noexcept
{
    log_layout.trace("{} Area::call_size({})\n", name, area->name);
    Log::Indenter indenter;

    if (area != nullptr)
    {
        area->do_size(size);
    }
}

void Area::end_size() noexcept
{
    assert_size_valid();

    log_layout.trace("{} Area::end_size() size = {}, {}\n",
                     name, size.x, size.y);
}

// Do not make this virtual.
// Derived classes should override BeginSize() instead
auto Area::do_size(glm::vec2 free_size_reference) noexcept
-> vec2
{
    log_layout.trace("{} Area::do_size() reference = {}, {}",
                     name, free_size_reference.x, free_size_reference.y);
    Log::Indenter indenter;

    begin_size(free_size_reference);
    for (auto* child : children)
    {
        call_size(child);
    }

    end_size();

    assert_size_valid();

    log_layout.trace("  {} size = {}, {}\n", name, size.x, size.y);

    return size;
}

void Area::begin_place(Rectangle reference, glm::vec2 container_grow_direction) noexcept
{
    static_cast<void>(container_grow_direction); // TODO(tksuoran@gmail.com): Check
    log_layout.trace("{} Area::begin_place() reference = ({}, {})..({}, {})\n",
                     name,
                     reference.min().x, reference.min().y,
                     reference.max().x, reference.max().y);
    Log::Indenter indenter;

    log_layout.trace("  {} size = {}, {}\n", name, size.x, size.y);

    switch (layout_style)
    {
        case Flow_mode::normal:
        {
            break;
        }

        case Flow_mode::extend_horizontal:
        {
            size.x = reference.size().x;
            break;
        }

        case Flow_mode::extend_vertical:
        {
            size.y = reference.size().y;
            break;
        }
    }

    log_layout.trace("  {} size = {}, {} after layout style\n", name, size.x, size.y);

    rect.min() = reference.min() +
                 offset_pixels +
                 reference.size() * offset_free_size_relative +
                 size * offset_self_size_relative;

    rect.max() = rect.min() + size;

    rect.min() = glm::round(rect.min());
    rect.max() = glm::round(rect.max());

    if (clip_to_reference)
    {
        rect.clip_to(reference);
    }

    in_rect = rect.shrink(style->padding);
}

void Area::call_place(gsl::not_null<Area*> area) noexcept
{
    log_layout.trace("{} Area::call_place({})\n", name, area->name);
    Log::Indenter indenter;

    if (area != nullptr)
    {
        area->do_place(rect, vec2(1.0f, 1.0f));
    }
}

void Area::end_place() noexcept
{
}

// Do not make this virtual.
// Derived classes should override BeginPlace() instead
auto Area::do_place(const Rectangle& reference_location,
                    glm::vec2        grow_direction) noexcept
-> vec2
{
    log_layout.trace("{} Area::do_place() reference size = {}, {}\n",
                     name, reference_location.size().x, reference_location.size().y);
    Log::Indenter indenter;

    log_layout.trace("  {} size = {}, {}\n", name, size.x, size.y);

    begin_place(reference_location, grow_direction);
    for (auto* child : children)
    {
        call_place(child);
    }

    end_place();
    return size;
}

} // namespace erhe::ui
