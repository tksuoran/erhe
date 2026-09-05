// USD purpose vocabulary (doc/usd-compatibility-plan.md M3): purpose is an
// inherited enumeration whose default layer is derived from the editor-only
// flag bits (doc/property-system.md D31).

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using namespace erhe::property;

class Leaf : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Leaf>
{
public:
    explicit Leaf(const std::string_view name) : Item{name} {}
    explicit Leaf(const Leaf& other) = default;
    static constexpr std::string_view static_type_name{"Leaf"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return uint64_t{1} << 57; }
};

[[nodiscard]] auto make_content_item(const std::string_view name) -> std::shared_ptr<Leaf>
{
    auto item = std::make_shared<Leaf>(name);
    item->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    return item;
}

} // namespace

TEST(Item_purpose, content_item_is_default)
{
    const std::shared_ptr<Leaf> item = make_content_item("i");
    EXPECT_EQ(item->get_purpose(), erhe::Purpose::default_);
    EXPECT_EQ(item->get_value_source(erhe::Item_base::purpose_property.get()), Value_source::default_value);
    EXPECT_FALSE(item->has_local_value(erhe::Item_base::purpose_property.get()));
}

TEST(Item_purpose, item_without_show_in_ui_is_guide)
{
    auto item = std::make_shared<Leaf>("i");
    EXPECT_EQ(item->get_purpose(), erhe::Purpose::guide);
    EXPECT_EQ(item->get_value_source(erhe::Item_base::purpose_property.get()), Value_source::default_value);
}

TEST(Item_purpose, editor_only_flags_derive_guide)
{
    const uint64_t bits[] = {
        erhe::Item_flags::tool,
        erhe::Item_flags::brush,
        erhe::Item_flags::controller,
        erhe::Item_flags::rendertarget
    };
    for (const uint64_t bit : bits) {
        const std::shared_ptr<Leaf> item = make_content_item("i");
        EXPECT_EQ(item->get_purpose(), erhe::Purpose::default_);
        item->enable_flag_bits(bit);
        EXPECT_EQ(item->get_purpose(), erhe::Purpose::guide);
        item->disable_flag_bits(bit);
        EXPECT_EQ(item->get_purpose(), erhe::Purpose::default_);
    }
}

TEST(Item_purpose, flag_change_notifies_observers)
{
    const std::shared_ptr<Leaf> item = make_content_item("i");
    int changes = 0;
    erhe::Purpose observed{erhe::Purpose::default_};
    const Observer_token token = item->add_observer(
        erhe::Item_base::purpose_property.get(),
        [&changes, &observed](Dependency_object&, const Property_changed_args& args) {
            ++changes;
            observed = get_as<erhe::Purpose>(args.new_value);
        }
    );
    item->enable_flag_bits(erhe::Item_flags::tool);
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(observed, erhe::Purpose::guide);

    // A flag that purpose is not derived from changes nothing.
    item->enable_flag_bits(erhe::Item_flags::show_debug_visualizations);
    EXPECT_EQ(changes, 1);
}

TEST(Item_purpose, local_value_overrides_the_derived_default)
{
    const std::shared_ptr<Leaf> item = make_content_item("i");
    item->enable_flag_bits(erhe::Item_flags::tool);
    EXPECT_EQ(item->get_purpose(), erhe::Purpose::guide);

    item->set_value(erhe::Item_base::purpose_property, erhe::Purpose::render);
    EXPECT_EQ(item->get_purpose(), erhe::Purpose::render);
    EXPECT_EQ(item->get_value_source(erhe::Item_base::purpose_property.get()), Value_source::local);

    // The default layer is still the derived value: clearing returns to it.
    EXPECT_TRUE(item->clear_value(erhe::Item_base::purpose_property.get()));
    EXPECT_EQ(item->get_purpose(), erhe::Purpose::guide);
    EXPECT_EQ(item->get_value_source(erhe::Item_base::purpose_property.get()), Value_source::default_value);
}

TEST(Item_purpose, authored_value_inherits_through_the_hierarchy)
{
    const std::shared_ptr<Leaf> root = make_content_item("root");
    const std::shared_ptr<Leaf> leaf = make_content_item("leaf");
    leaf->set_parent(root);

    EXPECT_EQ(leaf->get_purpose(), erhe::Purpose::default_);

    root->set_value(erhe::Item_base::purpose_property, erhe::Purpose::render);
    EXPECT_EQ(leaf->get_purpose(), erhe::Purpose::render);
    EXPECT_EQ(leaf->get_value_source(erhe::Item_base::purpose_property.get()), Value_source::inherited);

    // A descendant's own derived default is below the inherited value.
    leaf->enable_flag_bits(erhe::Item_flags::tool);
    EXPECT_EQ(leaf->get_purpose(), erhe::Purpose::render);

    root->clear_value(erhe::Item_base::purpose_property.get());
    EXPECT_EQ(leaf->get_purpose(), erhe::Purpose::guide);
    EXPECT_EQ(leaf->get_value_source(erhe::Item_base::purpose_property.get()), Value_source::default_value);
}

TEST(Item_purpose, an_unauthored_purpose_is_not_a_local_value)
{
    const std::shared_ptr<Leaf> item = make_content_item("i");
    item->enable_flag_bits(erhe::Item_flags::tool);
    int local_count = 0;
    item->for_each_local_value(
        [&local_count](const Dependency_property& property, const Property_value&) {
            if (&property == erhe::Item_base::purpose_property.get_ptr()) {
                ++local_count;
            }
        }
    );
    EXPECT_EQ(local_count, 0);
}
