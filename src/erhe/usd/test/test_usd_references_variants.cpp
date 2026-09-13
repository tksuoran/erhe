// A `variants` selection a composition arc carries into the target it brings
// in (doc/usd-compatibility-plan.md section 6, "Variant selection through a
// composition arc"). In LIVRPS such a selection is stronger than the target's
// own, so two carriers of one target prim compose two different prim trees:
// the reader reports the selection on every arc of the carrier, a load that is
// given one applies it before the target's own `variants` metadatum, and the
// writer authors it back beside the arcs.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_reference_variant_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] auto load(
    const std::filesystem::path&              path,
    const std::shared_ptr<erhe::scene::Node>& root,
    const erhe::usd::Usd_variant_selections&  variant_selections = erhe::usd::Usd_variant_selections{}
) -> erhe::usd::Usd_load_result
{
    const erhe::usd::Usd_load_arguments arguments{
        .path               = path,
        .root_node          = root,
        .mesh_layer_id      = 0,
        .stage_metrics      = erhe::usd::Stage_metrics::root,
        .variant_selections = variant_selections
    };
    return erhe::usd::load_usd(arguments);
}

[[nodiscard]] auto find_child(const erhe::Hierarchy& parent, const std::string& name) -> erhe::Hierarchy*
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : parent.get_children()) {
        if (child && (child->get_name() == name)) {
            return child.get();
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_references(
    const erhe::usd::Usd_data& data,
    const std::string&         stage_path
) -> const erhe::usd::Usd_prim_references*
{
    for (const erhe::usd::Usd_prim_references& entry : data.references) {
        if (entry.stage_path == stage_path) {
            return &entry;
        }
    }
    return nullptr;
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

[[nodiscard]] auto find_selection(
    const std::vector<erhe::usd::Usd_variant_selection>& selections,
    const std::string&                                   relative_path,
    const std::string&                                   set_name
) -> const erhe::usd::Usd_variant_selection*
{
    for (const erhe::usd::Usd_variant_selection& selection : selections) {
        if ((selection.relative_path == relative_path) && (selection.set_name == set_name)) {
            return &selection;
        }
    }
    return nullptr;
}

// One entry of a selection that names the prim it is measured from.
[[nodiscard]] auto make_selections(
    const std::string& root_prim_path,
    const std::string& relative_path,
    const std::string& set_name,
    const std::string& variant_name
) -> erhe::usd::Usd_variant_selections
{
    erhe::usd::Usd_variant_selections selections{};
    selections.root_prim_path = root_prim_path;
    selections.entries.push_back(
        erhe::usd::Usd_variant_selection{
            .relative_path = relative_path,
            .set_name      = set_name,
            .variant_name  = variant_name
        }
    );
    return selections;
}

// The arcs a prim authored itself, as the writer's records, carrying the
// selection the reader read off the carrier.
[[nodiscard]] auto to_save_references(
    const erhe::usd::Usd_data&   data,
    const std::filesystem::path& written_path
) -> std::vector<erhe::usd::Usd_save_prim_references>
{
    std::vector<erhe::usd::Usd_save_prim_references> save_references;
    for (const erhe::usd::Usd_prim_references& entry : data.references) {
        erhe::usd::Usd_save_prim_references save_entry{};
        save_entry.item = entry.item;
        for (const erhe::usd::Usd_reference& reference : entry.references) {
            if (!reference.variant_set.empty()) {
                continue;
            }
            save_entry.references.push_back(
                erhe::usd::Usd_save_reference{
                    .source_path        = written_path.parent_path() / std::filesystem::path{reference.asset_path},
                    .prim_path          = reference.prim_path,
                    .kind               = reference.kind,
                    .variant_selections = reference.variant_selections
                }
            );
        }
        if (!save_entry.references.empty()) {
            save_references.push_back(std::move(save_entry));
        }
    }
    return save_references;
}

// The variant sets of the referencing file itself - the carrier that has both
// its own set and an arc-carried selection.
[[nodiscard]] auto to_save_variant_sets(const erhe::usd::Usd_data& data) -> std::vector<erhe::usd::Usd_save_variant_set>
{
    std::vector<erhe::usd::Usd_save_variant_set> save_sets;
    for (const erhe::usd::Usd_variant_set& set : data.variant_sets) {
        erhe::usd::Usd_save_variant_set save_set{};
        save_set.item     = set.prim;
        save_set.set_name = set.set_name;
        save_set.selected = set.selected;
        erhe::Hierarchy* const carrier = dynamic_cast<erhe::Hierarchy*>(set.prim.get());
        for (const erhe::usd::Usd_variant& variant : set.variants) {
            erhe::usd::Usd_save_variant save_variant{};
            save_variant.name      = variant.name;
            save_variant.overrides = variant.overrides;
            if (carrier != nullptr) {
                for (const erhe::usd::Usd_variant_prim& variant_prim : variant.prims) {
                    erhe::Hierarchy* const item = find_child(*carrier, variant_prim.relative_path);
                    if (item == nullptr) {
                        continue;
                    }
                    save_variant.prims.push_back(
                        erhe::usd::Usd_save_variant_prim{
                            .item          = item->shared_from_this(),
                            .authored_name = variant_prim.authored_name
                        }
                    );
                }
            }
            save_set.variants.push_back(std::move(save_variant));
        }
        save_sets.push_back(std::move(save_set));
    }
    return save_sets;
}

TEST(Reference_variants, the_carriers_selection_is_read_off_every_arc)
{
    const std::shared_ptr<erhe::scene::Node> root   = std::make_shared<erhe::scene::Xform>("root");
    const erhe::usd::Usd_load_result         result = load(test_data_path("references_variants.usda"), root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const erhe::usd::Usd_prim_references* const fancy = find_references(result.data, "/World/FancyColumn");
    ASSERT_NE(fancy, nullptr);
    ASSERT_EQ(fancy->references.size(), 1u);
    const erhe::usd::Usd_variant_selection* const fancy_selection =
        find_selection(fancy->references[0].variant_selections, std::string{}, "shapeVariant");
    ASSERT_NE(fancy_selection, nullptr);
    EXPECT_EQ(fancy_selection->variant_name, "Fancy");

    // A `variants` metadatum on an `over` prim below the carrier is the entry
    // of that prim's path below the target.
    const erhe::usd::Usd_prim_references* const utah = find_references(result.data, "/World/UtahColumn");
    ASSERT_NE(utah, nullptr);
    ASSERT_EQ(utah->references.size(), 1u);
    EXPECT_EQ(utah->references[0].variant_selections.size(), 2u);
    const erhe::usd::Usd_variant_selection* const deeper =
        find_selection(utah->references[0].variant_selections, "Body", "shadingVariant");
    ASSERT_NE(deeper, nullptr);
    EXPECT_EQ(deeper->variant_name, "Red");

    // A set the referencing prim declares itself is its own selection, not one
    // it carries into the target.
    const erhe::usd::Usd_prim_references* const own = find_references(result.data, "/World/OwnSetColumn");
    ASSERT_NE(own, nullptr);
    ASSERT_EQ(own->references.size(), 1u);
    ASSERT_EQ(own->references[0].variant_selections.size(), 1u);
    EXPECT_EQ(own->references[0].variant_selections[0].set_name,     "shapeVariant");
    EXPECT_EQ(own->references[0].variant_selections[0].variant_name, "Fancy");
    const erhe::usd::Usd_variant_set* const own_set = find_set(result.data, "/World/OwnSetColumn", "localVariant");
    ASSERT_NE(own_set, nullptr);
    EXPECT_EQ(own_set->selected, "On");
}

TEST(Reference_variants, a_carried_selection_wins_over_the_targets_own)
{
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("root");
    const erhe::usd::Usd_load_result         result = load(
        test_data_path("references_variants_target.usda"),
        root,
        make_selections("/Teapot", std::string{}, "shapeVariant", "Fancy")
    );
    ASSERT_TRUE(result.error.empty()) << result.error;

    // The file's own selection is "Utah".
    const erhe::usd::Usd_variant_set* const set = find_set(result.data, "/Teapot", "shapeVariant");
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->selected, "Fancy");

    // The selected variant's arc is the one the carrying prim holds.
    const erhe::usd::Usd_prim_references* const entry = find_references(result.data, "/Teapot");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->references.size(), 1u);
    EXPECT_EQ(entry->references[0].prim_path,    "/Library/Gadget");
    EXPECT_EQ(entry->references[0].variant_name, "Fancy");

    // The hoist marked the prims of the variant that is not selected inactive.
    erhe::Hierarchy* const teapot = find_child(*root.get(), "Teapot");
    ASSERT_NE(teapot, nullptr);
    erhe::Hierarchy* const fancy_extras = find_child(*teapot, "FancyExtras");
    erhe::Hierarchy* const utah_extras  = find_child(*teapot, "UtahExtras");
    ASSERT_NE(fancy_extras, nullptr);
    ASSERT_NE(utah_extras,  nullptr);
    EXPECT_TRUE (fancy_extras->get_value(erhe::Item_base::active_property));
    EXPECT_FALSE(utah_extras ->get_value(erhe::Item_base::active_property));
}

TEST(Reference_variants, an_empty_selection_root_is_the_layers_default_prim)
{
    // An arc that names no prim path targets the layer's `defaultPrim`, which
    // the caller cannot know before the file is read: it leaves the root empty
    // and load_stage resolves it (the file declares `defaultPrim = "Teapot"`).
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("root");
    const erhe::usd::Usd_load_result         result = load(
        test_data_path("references_variants_target.usda"),
        root,
        make_selections(std::string{}, std::string{}, "shapeVariant", "Fancy")
    );
    ASSERT_TRUE(result.error.empty()) << result.error;
    // The root resolved, so nothing about the selection was dropped.
    EXPECT_EQ(result.warning.find("the selection is dropped"), std::string::npos) << result.warning;

    const erhe::usd::Usd_variant_set* const set = find_set(result.data, "/Teapot", "shapeVariant");
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->selected, "Fancy");

    erhe::Hierarchy* const teapot = find_child(*root.get(), "Teapot");
    ASSERT_NE(teapot, nullptr);
    erhe::Hierarchy* const fancy_extras = find_child(*teapot, "FancyExtras");
    erhe::Hierarchy* const utah_extras  = find_child(*teapot, "UtahExtras");
    ASSERT_NE(fancy_extras, nullptr);
    ASSERT_NE(utah_extras,  nullptr);
    EXPECT_TRUE (fancy_extras->get_value(erhe::Item_base::active_property));
    EXPECT_FALSE(utah_extras ->get_value(erhe::Item_base::active_property));
}

TEST(Reference_variants, a_selection_the_target_does_not_declare_is_dropped)
{
    erhe::usd::Usd_variant_selections selections = make_selections("/Teapot", std::string{}, "shapeVariant", "Nonexistent");
    selections.entries.push_back(
        erhe::usd::Usd_variant_selection{
            .relative_path = std::string{},
            .set_name      = "noSuchSet",
            .variant_name  = "Whatever"
        }
    );
    const erhe::usd::Load_stage_result stage_result = erhe::usd::load_stage(
        test_data_path("references_variants_target.usda"),
        selections
    );
    ASSERT_TRUE(stage_result.error.empty()) << stage_result.error;
    EXPECT_NE(stage_result.warning.find("holds no such variant"),        std::string::npos) << stage_result.warning;
    EXPECT_NE(stage_result.warning.find("declares no such variant set"), std::string::npos) << stage_result.warning;

    // Both entries dropped, so the target's own selection stands.
    const std::shared_ptr<erhe::scene::Node> root   = std::make_shared<erhe::scene::Xform>("root");
    const erhe::usd::Usd_load_result         result = load(
        test_data_path("references_variants_target.usda"),
        root,
        selections
    );
    ASSERT_TRUE(result.error.empty()) << result.error;
    const erhe::usd::Usd_variant_set* const set = find_set(result.data, "/Teapot", "shapeVariant");
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->selected, "Utah");
}

TEST(Reference_variants, save_writes_the_selection_beside_the_arcs)
{
    const std::filesystem::path source_path  = test_data_path("references_variants.usda");
    const std::filesystem::path written_path = temporary_path("references_variants_written.usda");

    const std::shared_ptr<erhe::scene::Node> root   = std::make_shared<erhe::scene::Xform>("root");
    const erhe::usd::Usd_load_result         result = load(source_path, root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const erhe::usd::Usd_save_arguments save_arguments{
        .path         = written_path,
        .root_node    = root,
        .references   = to_save_references(result.data, written_path),
        .variant_sets = to_save_variant_sets(result.data)
    };
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;

    const std::string written = read_file(written_path);
    EXPECT_NE(written.find("string shapeVariant = \"Fancy\""), std::string::npos) << written;
    EXPECT_NE(written.find("references = @references_variants_target.usda@</Teapot>"), std::string::npos) << written;

    // The deeper entry is authored inside the `over` prim of its path.
    const std::size_t body = written.find("over \"Body\"");
    ASSERT_NE(body, std::string::npos) << written;
    EXPECT_NE(written.find("string shadingVariant = \"Red\"", body), std::string::npos) << written;

    // The reload reads the same selections back off the carriers.
    const std::shared_ptr<erhe::scene::Node> reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_result         reloaded      = load(written_path, reloaded_root);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    const erhe::usd::Usd_prim_references* const fancy = find_references(reloaded.data, "/World/FancyColumn");
    ASSERT_NE(fancy, nullptr);
    ASSERT_EQ(fancy->references.size(), 1u);
    const erhe::usd::Usd_variant_selection* const fancy_selection =
        find_selection(fancy->references[0].variant_selections, std::string{}, "shapeVariant");
    ASSERT_NE(fancy_selection, nullptr);
    EXPECT_EQ(fancy_selection->variant_name, "Fancy");
    const erhe::usd::Usd_prim_references* const utah = find_references(reloaded.data, "/World/UtahColumn");
    ASSERT_NE(utah, nullptr);
    ASSERT_EQ(utah->references.size(), 1u);
    EXPECT_NE(find_selection(utah->references[0].variant_selections, "Body", "shadingVariant"), nullptr);
}

TEST(Reference_variants, a_carrier_keeps_its_own_sets_selection_and_the_arcs)
{
    const std::filesystem::path source_path  = test_data_path("references_variants.usda");
    const std::filesystem::path written_path = temporary_path("references_variants_own_set.usda");

    const std::shared_ptr<erhe::scene::Node> root   = std::make_shared<erhe::scene::Xform>("root");
    const erhe::usd::Usd_load_result         result = load(source_path, root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const erhe::usd::Usd_save_arguments save_arguments{
        .path         = written_path,
        .root_node    = root,
        .references   = to_save_references(result.data, written_path),
        .variant_sets = to_save_variant_sets(result.data)
    };
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;

    // The carrier declares `localVariant` itself and carries `shapeVariant`
    // into the target: its `variants` metadatum holds both.
    const std::string written = read_file(written_path);
    const std::size_t prim    = written.find("\"OwnSetColumn\"");
    ASSERT_NE(prim, std::string::npos) << written;
    const std::size_t block   = written.find("variantSet \"localVariant\"", prim);
    ASSERT_NE(block, std::string::npos) << written;
    const std::string metas   = written.substr(prim, block - prim);
    EXPECT_NE(metas.find("string localVariant = \"On\""),   std::string::npos) << metas;
    EXPECT_NE(metas.find("string shapeVariant = \"Fancy\""), std::string::npos) << metas;
}

} // anonymous namespace
