// Minimal definitions of the editor functions property_edit_operation.cpp
// and item_property_apply.cpp call that need the whole editor (scenes,
// asset manager, draw lists, the rig's node transform operations), for
// editor_operation_tests. They record what
// they were asked; the reference check refuses the items the test marks
// foreign.

#include "editor_glue.hpp"

#include "app_context.hpp"
#include "assets/asset_reference.hpp"
#include "operations/property_set_operation.hpp"
#include "rig/bone_connect.hpp"

#include "erhe_item/item.hpp"

namespace editor::test {

void Editor_glue::reset()
{
    property_changed.clear();
    adopted_userships.clear();
    foreign_items.clear();
}

auto get_editor_glue() -> Editor_glue&
{
    static Editor_glue glue;
    return glue;
}

}

namespace editor {

void App_context::on_item_property_changed(erhe::Item_base& item, const erhe::property::Dependency_property& property)
{
    test::get_editor_glue().property_changed.push_back(
        test::Editor_glue::Property_changed{.item = &item, .property = &property}
    );
}

Asset_reference::Asset_reference() = default;
Asset_reference::Asset_reference(Asset_reference&& other) noexcept = default;
Asset_reference& Asset_reference::operator=(Asset_reference&& other) noexcept = default;
Asset_reference::~Asset_reference() noexcept = default;

auto is_item_reference_allowed(App_context& context, const erhe::Item_base& target, const erhe::Item_base& referenced) -> bool
{
    static_cast<void>(context);
    static_cast<void>(target);
    return !test::get_editor_glue().foreign_items.contains(&referenced);
}

// The tests construct no Property_edit_operation with
// Property_edit_follow_ups::bone_connect; bone follow-ups are covered by the
// MCP tests against the whole editor.
void append_bone_connect_follow_ups(
    const erhe::Item_base&                     item,
    const erhe::property::Dependency_property& property,
    std::vector<std::shared_ptr<Operation>>&   out_operations
)
{
    static_cast<void>(item);
    static_cast<void>(property);
    static_cast<void>(out_operations);
}

void adopt_reference_usership(App_context& context, std::vector<Asset_reference>& userships, const std::shared_ptr<erhe::Item_base>& item)
{
    static_cast<void>(context);
    static_cast<void>(userships);
    if (item) {
        test::get_editor_glue().adopted_userships.insert(item.get());
    }
}

}
