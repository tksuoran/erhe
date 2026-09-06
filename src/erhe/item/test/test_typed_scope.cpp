// The prim class hierarchy (doc/usd-compatibility-plan.md C5, step U1):
// Typed carries the USD typeName token and Scope is a typed prim that holds
// children only and category values for its descendants (D30).

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_property/dependency_property.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using namespace erhe::property;

// A class off the Typed chain, so its properties can only reach a Scope
// through the secondary owner type (D30).
class Prim_category_probe : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Prim_category_probe>
{
public:
    explicit Prim_category_probe(const std::string_view name) : Item{name} {}
    explicit Prim_category_probe(const Prim_category_probe& other) = default;
    static constexpr std::string_view static_type_name{"Prim_category_probe"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return uint64_t{1} << 58; }

    static const Property<int> hue_property;
};

const Property<int> Prim_category_probe::hue_property = Property<int>::register_property(
    "hue", Prim_category_probe::property_owner_type(),
    Property_metadata{.default_value = make_value(1), .inherits = true}
);

} // namespace

TEST(Typed_scope, composed_type_bits)
{
    const std::shared_ptr<erhe::Typed> typed = std::make_shared<erhe::Typed>("Cube", "Cube");
    const std::shared_ptr<erhe::Scope> scope = std::make_shared<erhe::Scope>("Materials");

    EXPECT_TRUE (erhe::is<erhe::Typed>(typed));
    EXPECT_TRUE (erhe::is<erhe::Typed>(scope));
    EXPECT_TRUE (erhe::is<erhe::Scope>(scope));
    EXPECT_FALSE(erhe::is<erhe::Scope>(typed));
    EXPECT_EQ(erhe::Scope::get_static_type(), erhe::Item_type::typed | erhe::Item_type::scope);
}

TEST(Typed_scope, class_type_names)
{
    const std::shared_ptr<erhe::Typed> typed = std::make_shared<erhe::Typed>("Cube", "Cube");
    const std::shared_ptr<erhe::Scope> scope = std::make_shared<erhe::Scope>("Materials");

    EXPECT_EQ(typed->get_type_name(), "Typed");
    EXPECT_EQ(scope->get_type_name(), "Scope");
}

TEST(Typed_scope, typed_prim_type_name_is_authored)
{
    const std::shared_ptr<erhe::Typed> typed = std::make_shared<erhe::Typed>("thing");
    EXPECT_EQ(typed->get_prim_type_name(), "");
    EXPECT_EQ(typed->get_value(erhe::Typed::type_name_property), "");

    EXPECT_TRUE(typed->set_value(erhe::Typed::type_name_property.get(), make_value(std::string{"PointInstancer"})));
    EXPECT_EQ(typed->get_prim_type_name(), "PointInstancer");
    EXPECT_EQ(typed->get_value(erhe::Typed::type_name_property), "PointInstancer");

    typed->set_prim_type_name("SkelRoot");
    EXPECT_EQ(typed->get_value(erhe::Typed::type_name_property), "SkelRoot");
}

TEST(Typed_scope, scope_prim_type_name_is_fixed_by_the_class)
{
    const std::shared_ptr<erhe::Scope> scope = std::make_shared<erhe::Scope>("Materials");
    EXPECT_EQ(scope->get_class_type_name(), "Scope");
    EXPECT_EQ(scope->get_prim_type_name(), "Scope");
    EXPECT_EQ(scope->get_value(erhe::Typed::type_name_property), "Scope");

    std::string error;
    EXPECT_FALSE(scope->validate_value(erhe::Typed::type_name_property.get(), make_value(std::string{"Xform"}), error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(scope->set_value(erhe::Typed::type_name_property.get(), make_value(std::string{"Xform"})));
    EXPECT_EQ(scope->get_prim_type_name(), "Scope");
}

TEST(Typed_scope, path_through_a_scope)
{
    const std::shared_ptr<erhe::Scope> root  = std::make_shared<erhe::Scope>("root");
    const std::shared_ptr<erhe::Scope> scope = std::make_shared<erhe::Scope>("Materials");
    const std::shared_ptr<erhe::Typed> typed = std::make_shared<erhe::Typed>("Cube", "Cube");
    scope->set_parent(root);
    typed->set_parent(scope);

    EXPECT_EQ(typed->get_path(), "Materials/Cube");
    EXPECT_EQ(erhe::find_by_path(*root, "Materials/Cube"), typed.get());
}

TEST(Typed_scope, scope_holds_category_values_for_descendants)
{
    const std::shared_ptr<erhe::Scope>  scope  = std::make_shared<erhe::Scope>("Prim_category_probes");
    const std::shared_ptr<Prim_category_probe> probe = std::make_shared<Prim_category_probe>("w");
    const std::shared_ptr<erhe::Typed>  typed  = std::make_shared<erhe::Typed>("Cube", "Cube");
    probe->set_parent(scope);

    const Property_registry& registry = Property_registry::get();
    // The qualified name resolves on a Scope through its secondary owner
    // type, and on a plain Typed - which has none - it does not.
    EXPECT_EQ(registry.find_for_object(*scope, "Prim_category_probe.hue"), &Prim_category_probe::hue_property.get());
    EXPECT_EQ(registry.find_for_object(*typed, "Prim_category_probe.hue"), nullptr);
    EXPECT_TRUE(registry.is_secondary_property(*scope, Prim_category_probe::hue_property.get()));
    EXPECT_EQ(registry.qualified_name(*scope, Prim_category_probe::hue_property.get()), "Prim_category_probe.hue");

    EXPECT_EQ(probe->get_value(Prim_category_probe::hue_property), 1);
    EXPECT_TRUE(scope->set_value(Prim_category_probe::hue_property.get(), make_value(7)));
    EXPECT_EQ(probe->get_value(Prim_category_probe::hue_property), 7);
    EXPECT_EQ(probe->get_value_source(Prim_category_probe::hue_property.get()), Value_source::inherited);
}
