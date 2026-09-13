// What a variant authors for a prim a composition arc supplies
// (doc/usd-compatibility-plan.md C6, section 6 "Variant opinions a variant set
// does not carry"). erhe composes no arc: the caller instantiates each one
// after the load returns, so a variant path the reader cannot reach is not a
// path that names nothing. The reader hands such an opinion or binding over as
// pending, and only a path that crosses no prim authoring arcs stays dropped
// and counted.

#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto load(const std::filesystem::path& path, const std::shared_ptr<erhe::scene::Node>& root) -> erhe::usd::Usd_load_result
{
    const erhe::usd::Usd_load_arguments arguments{
        .path          = path,
        .root_node     = root,
        .mesh_layer_id = 0
    };
    return erhe::usd::load_usd(arguments);
}

[[nodiscard]] auto find_set(
    const erhe::usd::Usd_data& data,
    const std::string&         stage_path,
    const std::string&         set_name
) -> const erhe::usd::Usd_variant_set*
{
    for (const erhe::usd::Usd_variant_set& set : data.variant_sets) {
        if ((set.stage_path == stage_path) && (set.set_name == set_name)) {
            return &set;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_variant(
    const erhe::usd::Usd_variant_set& set,
    const std::string&                name
) -> const erhe::usd::Usd_variant*
{
    for (const erhe::usd::Usd_variant& variant : set.variants) {
        if (variant.name == name) {
            return &variant;
        }
    }
    return nullptr;
}

class Variant_pending_opinions : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("root");
        result = load(test_data_path("variant_pending_opinions.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Xform> root;
    erhe::usd::Usd_load_result          result;
};

} // anonymous namespace

// The carrier references an internal target, so `plate` is a prim the arc owes
// and no prim of this load. The opinion waits for the caller instead of being
// dropped.
TEST_F(Variant_pending_opinions, an_opinion_behind_an_arc_is_pending)
{
    const erhe::usd::Usd_variant_set* const set = find_set(result.data, "/Carrier", "shadingVariant");
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->selected, "Red");
    EXPECT_EQ(set->unsupported_opinion_count, 0u);

    const erhe::usd::Usd_variant* const red = find_variant(*set, "Red");
    ASSERT_NE(red, nullptr);
    EXPECT_TRUE(red->overrides.empty());
    ASSERT_EQ(red->pending_overrides.size(), 1u);
    EXPECT_EQ(red->pending_overrides.front().relative_path, "plate");
    ASSERT_EQ(red->pending_overrides.front().values.size(), 1u);
    EXPECT_EQ(red->pending_overrides.front().values.front().name, "Gprim.display_color");
}

// Every variant is triaged, not only the selected one: the caller carries the
// pending entries into the scene's variant table for a later switch.
TEST_F(Variant_pending_opinions, an_unselected_variants_opinion_is_pending_too)
{
    const erhe::usd::Usd_variant_set* const set = find_set(result.data, "/Carrier", "shadingVariant");
    ASSERT_NE(set, nullptr);

    const erhe::usd::Usd_variant* const blue = find_variant(*set, "Blue");
    ASSERT_NE(blue, nullptr);
    EXPECT_TRUE(blue->overrides.empty());
    EXPECT_EQ(blue->pending_overrides.size(), 1u);
    EXPECT_EQ(blue->pending_bindings.size(), 1u);
}

TEST_F(Variant_pending_opinions, a_binding_behind_an_arc_is_pending)
{
    const erhe::usd::Usd_variant_set* const set = find_set(result.data, "/Carrier", "shadingVariant");
    ASSERT_NE(set, nullptr);

    const erhe::usd::Usd_variant* const red = find_variant(*set, "Red");
    ASSERT_NE(red, nullptr);
    EXPECT_TRUE(red->bindings.empty());
    ASSERT_EQ(red->pending_bindings.size(), 1u);
    EXPECT_EQ(red->pending_bindings.front().relative_path, "plate");
    EXPECT_EQ(red->pending_bindings.front().material_path, "/Materials/Red");
}

// A pending entry's base value is the caller's to capture: the prim it names
// is not in the tree while the reader runs.
TEST_F(Variant_pending_opinions, no_base_value_is_captured_for_a_pending_entry)
{
    const erhe::usd::Usd_variant_set* const set = find_set(result.data, "/Carrier", "shadingVariant");
    ASSERT_NE(set, nullptr);
    EXPECT_TRUE(set->base_values.empty());
}

// The control: the prim carrying this set authors no arc, so nothing can
// supply the path and the opinion stays dropped and counted.
TEST_F(Variant_pending_opinions, an_opinion_no_arc_can_supply_is_still_counted)
{
    const erhe::usd::Usd_variant_set* const set = find_set(result.data, "/Plain", "shadingVariant");
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->unsupported_opinion_count, 1u);

    const erhe::usd::Usd_variant* const only = find_variant(*set, "Only");
    ASSERT_NE(only, nullptr);
    EXPECT_TRUE(only->overrides.empty());
    EXPECT_TRUE(only->pending_overrides.empty());
}
