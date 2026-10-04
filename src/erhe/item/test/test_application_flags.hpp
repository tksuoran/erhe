#pragma once

#include "erhe_item/item_flags.hpp"
#include "erhe_property/dependency_property.hpp"

#include <cstdint>

// The application flag bits the item tests stand in for the editor's with:
// Item_flags names none of them, so a test of the purpose derivation, the
// transient mask or a flag-bridged property registers its own set
// (register_test_application_flags(), from the test main).
class Test_item_flags
{
public:
    static constexpr uint64_t tool                      = erhe::Item_flags::application_bit(0);
    static constexpr uint64_t brush                     = erhe::Item_flags::application_bit(1);
    static constexpr uint64_t controller                = erhe::Item_flags::application_bit(2);
    static constexpr uint64_t rendertarget              = erhe::Item_flags::application_bit(3);
    static constexpr uint64_t show_debug_visualizations = erhe::Item_flags::application_bit(4);
    static constexpr uint64_t lock_viewport_selection   = erhe::Item_flags::application_bit(5);
    static constexpr uint64_t hovered_in_viewport       = erhe::Item_flags::application_bit(6);
    static constexpr uint64_t hovered_in_item_tree      = erhe::Item_flags::application_bit(7);

    static constexpr uint64_t transient              = hovered_in_viewport | hovered_in_item_tree;
    static constexpr uint64_t purpose_guide_when_set = tool | brush | controller | rendertarget;
};

class Test_item_properties
{
public:
    static const erhe::property::Property<bool> lock_viewport_selection_property;
};

void register_test_application_flags();
