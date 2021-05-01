#include "erhe/ui/choice.hpp"

namespace erhe::ui
{

Choice::Item::Item(Gui_renderer&               renderer,
                   const std::string&          label,
                   gsl::not_null<const Style*> style)
    : Button{renderer, label, style}
{
    name = "choice item: " + label;
}

void Choice::Item::action(Action_source* source)
{
    static_cast<void>(source);
    VERIFY(m_choice != nullptr);
    auto* selected = m_choice->selected();
    if (selected != this)
    {
        m_choice->set_selected(this);
    }
}

void Choice::Item::connect(Choice* choice)
{
    m_choice = choice;
}

Choice::Choice(Gui_renderer&               renderer,
               gsl::not_null<const Style*> style,
               gsl::not_null<const Style*> choice_item_style,
               Orientation                 orientation_in)
    : Dock               {renderer, style, orientation_in}
    , m_choice_item_style{choice_item_style}
{
    name = "choice";
}

auto Choice::add_item(std::shared_ptr<Choice::Item> item)
-> std::shared_ptr<Choice::Item>
{
    item->connect(this);

    switch (orientation())
    {
        case Orientation::horizontal:
        {
            item->layout_style = Flow_mode::extend_vertical;
            break;
        }

        case Orientation::vertical:
        {
            item->layout_style = Flow_mode::extend_horizontal;
            break;
        }
    }

    m_items.push_back(item);
    Dock::add(item.get());
    if (m_selected != item.get())
    {
        item->set_pressed(false);
    }

    return item;
}

void Choice::set_selected(Choice::Item* new_selected)
{
    auto* old_selected = m_selected;

    if (new_selected != old_selected)
    {
        if (old_selected != nullptr)
        {
            old_selected->set_pressed(false);
        }

        m_selected = new_selected;
        if (new_selected != nullptr)
        {
            new_selected->set_pressed(true);
        }

        if (sink() != nullptr)
        {
            sink()->action(m_selected);
        }
    }
}

auto Choice::make_item(const std::string& label, bool select)
-> std::shared_ptr<Choice::Item>
{
    auto item = std::make_shared<Item>(renderer, label, m_choice_item_style);
    // Choice_item handles actions itself
    item->set_sink(item.get());
    add_item(item);
    if (select)
    {
        set_selected(item.get());
    }

    return item;
}

} // namespace erhe::ui
