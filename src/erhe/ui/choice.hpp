#ifndef choice_hpp_erhe_ui
#define choice_hpp_erhe_ui

#include "erhe/ui/dock.hpp"
#include "erhe/ui/push_button.hpp"
#include <memory>

namespace erhe::ui
{

class Choice;
class Button;


class Choice
    : public Dock
    , public Action_source
{
public:

    class Item
        : public Button
        , public Action_sink
    {
    public:
        Item(Gui_renderer&               renderer,
             const std::string&          label,
             gsl::not_null<const Style*> style);

        virtual ~Item() = default;

        auto label() const
        -> const std::string&
        {
            return m_label;
        }

        void set_label(const std::string& value)
        {
            m_label = value;
        }

        void action(Action_source* source);

        void connect(Choice* choice);

    private:
        Choice*     m_choice{nullptr};
        std::string m_label;
    };

    Choice(Gui_renderer&               renderer,
           gsl::not_null<const Style*> style,
           gsl::not_null<const Style*> choice_item_style,
           Orientation                 orientation_in);

    virtual ~Choice() = default;

    auto items() const
    -> const std::vector<std::shared_ptr<Item>>&
    {
        return m_items;
    }

    auto selected() const
    -> Item*
    {
        return m_selected;
    }

    void set_selected(Item* new_selected);

    auto add_item(std::shared_ptr<Item> item)
    -> std::shared_ptr<Item>;

    auto make_item(const std::string& label, bool select = false)
    -> std::shared_ptr<Item>;

private:
    Item*                              m_selected{nullptr};
    std::vector<std::shared_ptr<Item>> m_items;
    gsl::not_null<const Style*>        m_choice_item_style;
};

} // namespace erhe::ui

#endif
