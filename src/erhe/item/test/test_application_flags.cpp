#include "test_application_flags.hpp"

#include "erhe_item/item.hpp"

namespace {

constexpr erhe::Item_flag_info c_test_item_flags[] = {
    { Test_item_flags::tool,                      "Tool",                 "tool"                      },
    { Test_item_flags::brush,                     "Brush",                "brush"                     },
    { Test_item_flags::controller,                "Controller",           "controller"                },
    { Test_item_flags::rendertarget,              "Rendertarget",         "rendertarget"              },
    { Test_item_flags::show_debug_visualizations, "Show Debug",           "show_debug_visualizations" },
    { Test_item_flags::lock_viewport_selection,   "Lock Selection",       "lock_viewport_selection"   },
    { Test_item_flags::hovered_in_viewport,       "Hovered in Viewport",  nullptr                     },
    { Test_item_flags::hovered_in_item_tree,      "Hovered in Item Tree", nullptr                     }
};

} // anonymous namespace

const erhe::property::Property<bool> Test_item_properties::lock_viewport_selection_property = erhe::Item_base::register_flag_bit_property(
    "lock_viewport_selection", erhe::Item_base::property_owner_type(), Test_item_flags::lock_viewport_selection,
    erhe::property::Property_ui{.group = "Locks", .label = "Selection"}
);

void register_test_application_flags()
{
    erhe::Item_flags::register_application_flags(
        c_test_item_flags,
        Test_item_flags::transient,
        Test_item_flags::purpose_guide_when_set
    );
}
