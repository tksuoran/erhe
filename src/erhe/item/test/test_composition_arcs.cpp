// The composition arcs of a prim (doc/erhe/item.md "Composition arcs",
// doc/plans/node_attachments_to_properties.md D4): prim-held storage that is
// absent while the prim carries no arc, deep-copied by a clone so a pasted
// instance is an instance of the same source, and rendered for the Properties
// window by a read-only computed property that is shown only while an arc
// stands.

#include "erhe_item/composition_arc.hpp"
#include "erhe_item/typed.hpp"

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_metadata.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto two_arcs() -> std::vector<erhe::Composition_arc>
{
    return std::vector<erhe::Composition_arc>{
        erhe::Composition_arc{
            .source_path        = "assets/teapot.usda",
            .prim_path          = "/Teapot",
            .name               = "Teapot",
            .kind               = erhe::Composition_arc_kind::reference,
            .variant_selections = {
                erhe::Composition_variant_selection{.relative_path = {}, .set_name = "shadingVariant", .variant_name = "Fancy"}
            }
        },
        erhe::Composition_arc{
            .source_path = "assets/lid.glb",
            .name        = "Lid",
            .kind        = erhe::Composition_arc_kind::payload
        }
    };
}

[[nodiscard]] auto is_arcs_row_visible(const erhe::Typed& prim) -> bool
{
    const erhe::property::Property_ui::Visible_when& visible_when =
        erhe::Typed::composition_arcs_property.get().get_metadata(prim.get_property_owner_type()).ui.visible_when;
    return visible_when && visible_when(prim);
}

} // anonymous namespace

TEST(Composition_arcs, a_prim_carries_no_arc_until_one_is_authored)
{
    const std::shared_ptr<erhe::Typed> prim = std::make_shared<erhe::Typed>("prim");
    EXPECT_FALSE(prim->has_composition_arcs());
    EXPECT_TRUE(prim->get_composition_arcs().empty());
}

TEST(Composition_arcs, authored_arcs_are_reported_in_order)
{
    const std::shared_ptr<erhe::Typed> prim = std::make_shared<erhe::Typed>("prim");
    prim->set_composition_arcs(two_arcs());

    ASSERT_TRUE(prim->has_composition_arcs());
    const std::span<const erhe::Composition_arc> arcs = prim->get_composition_arcs();
    ASSERT_EQ(arcs.size(), 2u);
    EXPECT_EQ(arcs[0].prim_path, "/Teapot");
    EXPECT_EQ(arcs[0].kind, erhe::Composition_arc_kind::reference);
    ASSERT_EQ(arcs[0].variant_selections.size(), 1u);
    EXPECT_EQ(arcs[0].variant_selections[0].variant_name, "Fancy");
    EXPECT_EQ(arcs[1].name, "Lid");
    EXPECT_EQ(arcs[1].kind, erhe::Composition_arc_kind::payload);
}

TEST(Composition_arcs, an_empty_list_releases_the_storage)
{
    const std::shared_ptr<erhe::Typed> prim = std::make_shared<erhe::Typed>("prim");
    prim->set_composition_arcs(two_arcs());
    ASSERT_TRUE(prim->has_composition_arcs());

    prim->set_composition_arcs(std::vector<erhe::Composition_arc>{});
    EXPECT_FALSE(prim->has_composition_arcs());
    EXPECT_TRUE(prim->get_composition_arcs().empty());
}

TEST(Composition_arcs, a_clone_carries_its_own_copy_of_the_list)
{
    const std::shared_ptr<erhe::Typed> prim = std::make_shared<erhe::Typed>("prim");
    prim->set_composition_arcs(two_arcs());

    const std::shared_ptr<erhe::Item_base> clone_item = prim->clone();
    ASSERT_TRUE(clone_item);
    const std::shared_ptr<erhe::Typed> clone = std::dynamic_pointer_cast<erhe::Typed>(clone_item);
    ASSERT_TRUE(clone);

    ASSERT_TRUE(clone->has_composition_arcs());
    const std::span<const erhe::Composition_arc> source_arcs = prim->get_composition_arcs();
    const std::span<const erhe::Composition_arc> clone_arcs  = clone->get_composition_arcs();
    ASSERT_EQ(clone_arcs.size(), source_arcs.size());
    for (std::size_t index = 0, end = clone_arcs.size(); index < end; ++index) {
        EXPECT_EQ(clone_arcs[index], source_arcs[index]);
    }
    EXPECT_NE(clone_arcs.data(), source_arcs.data());

    // The copy is the clone's own: releasing the source's list leaves it.
    prim->set_composition_arcs(std::vector<erhe::Composition_arc>{});
    EXPECT_FALSE(prim->has_composition_arcs());
    EXPECT_TRUE(clone->has_composition_arcs());
    EXPECT_EQ(clone->get_composition_arcs().size(), 2u);
}

TEST(Composition_arcs, a_clone_of_a_prim_without_arcs_carries_none)
{
    const std::shared_ptr<erhe::Typed>    prim       = std::make_shared<erhe::Typed>("prim");
    const std::shared_ptr<erhe::Item_base> clone_item = prim->clone();
    ASSERT_TRUE(clone_item);
    const std::shared_ptr<erhe::Typed> clone = std::dynamic_pointer_cast<erhe::Typed>(clone_item);
    ASSERT_TRUE(clone);
    EXPECT_FALSE(clone->has_composition_arcs());
}

TEST(Composition_arcs, the_computed_row_is_shown_only_while_an_arc_stands)
{
    const std::shared_ptr<erhe::Typed> prim = std::make_shared<erhe::Typed>("prim");
    EXPECT_FALSE(is_arcs_row_visible(*prim.get()));

    prim->set_composition_arcs(two_arcs());
    EXPECT_TRUE(is_arcs_row_visible(*prim.get()));

    prim->set_composition_arcs(std::vector<erhe::Composition_arc>{});
    EXPECT_FALSE(is_arcs_row_visible(*prim.get()));
}

TEST(Composition_arcs, the_computed_row_spells_one_line_per_arc)
{
    const std::shared_ptr<erhe::Typed> prim = std::make_shared<erhe::Typed>("prim");
    prim->set_composition_arcs(two_arcs());

    const std::string text = prim->get_value(erhe::Typed::composition_arcs_property);
    EXPECT_EQ(
        text,
        "reference assets/teapot.usda [/Teapot] variants: . shadingVariant = Fancy\n"
        "payload assets/lid.glb"
    );
}
