#pragma once

#include <unordered_set>
#include <vector>

namespace erhe           { class Item_base; }
namespace erhe::property { class Dependency_property; }

namespace editor::test {

// What the test definitions of the editor functions saw (editor_glue.cpp).
class Editor_glue
{
public:
    class Property_changed
    {
    public:
        const erhe::Item_base*                     item    {nullptr};
        const erhe::property::Dependency_property* property{nullptr};
    };

    std::vector<Property_changed>               property_changed;   // App_context::on_item_property_changed calls
    std::unordered_set<const erhe::Item_base*>  adopted_userships;  // adopt_reference_usership items
    std::unordered_set<const erhe::Item_base*>  foreign_items;      // is_item_reference_allowed refuses these

    void reset();
};

[[nodiscard]] auto get_editor_glue() -> Editor_glue&;

}
