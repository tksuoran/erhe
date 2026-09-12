// Composition authored inside a variant block (doc/usd-compatibility-plan.md
// section 6, "Composition authored inside a variant block"). A variant block
// authors composition arcs of its own, and the prim carrying the set holds
// the selected variant's arcs: the reader reports them on that prim, named by
// the block they came from, and the writer puts them back inside that block.
// The `def` children of a variant block are the variant's own content, so a
// carrier converts them even though the prims its arcs bring in are the
// targets' business.

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
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_variant_arc_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
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

[[nodiscard]] auto find_child(const erhe::Hierarchy& parent, const std::string& name) -> erhe::Hierarchy*
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : parent.get_children()) {
        if (child && (child->get_name() == name)) {
            return child.get();
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_descendant(const erhe::Hierarchy& parent, const std::string& name) -> erhe::Hierarchy*
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : parent.get_children()) {
        if (!child) {
            continue;
        }
        if (child->get_name() == name) {
            return child.get();
        }
        erhe::Hierarchy* const found = find_descendant(*child.get(), name);
        if (found != nullptr) {
            return found;
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
        // A file with several top-level prims is written below a `World`
        // wrapper, so the path a reload spells is the tail of the one the
        // source spelled.
        if (
            (entry.stage_path == stage_path) ||
            ((entry.stage_path.size() > stage_path.size()) &&
             (entry.stage_path.compare(entry.stage_path.size() - stage_path.size(), stage_path.size(), stage_path) == 0))
        ) {
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

// The writer's table, the way the editor builds it: every variant's prims, and
// the arcs the selected variant authored.
[[nodiscard]] auto to_save_variant_sets(
    const erhe::usd::Usd_data&   data,
    const std::filesystem::path& source_path
) -> std::vector<erhe::usd::Usd_save_variant_set>
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
            for (const erhe::usd::Usd_reference& reference : variant.references) {
                save_variant.references.push_back(
                    erhe::usd::Usd_save_reference{
                        .source_path = source_path.parent_path() / std::filesystem::path{reference.asset_path},
                        .prim_path   = reference.prim_path,
                        .kind        = reference.kind
                    }
                );
            }
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

// The arcs a prim authored itself, as the writer's records: an arc a variant
// authored is not among them - the variant block writes that one.
[[nodiscard]] auto to_save_references(
    const erhe::usd::Usd_data&   data,
    const std::filesystem::path& source_path
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
                    .source_path = source_path.parent_path() / std::filesystem::path{reference.asset_path},
                    .prim_path   = reference.prim_path,
                    .kind        = reference.kind
                }
            );
        }
        if (!save_entry.references.empty()) {
            save_references.push_back(std::move(save_entry));
        }
    }
    return save_references;
}

TEST(Variant_arcs, selected_variant_arc_reaches_the_carrier)
{
    const std::shared_ptr<erhe::scene::Node> root   = std::make_shared<erhe::scene::Xform>("root");
    const erhe::usd::Usd_load_result         result = load(test_data_path("variant_arcs.usda"), root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const erhe::usd::Usd_prim_references* const entry = find_references(result.data, "/Carrier");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->references.size(), 1u);
    EXPECT_EQ(entry->references[0].asset_path,   "./reftarget.usda");
    EXPECT_EQ(entry->references[0].prim_path,    "/Widget");
    EXPECT_EQ(entry->references[0].kind,         erhe::usd::Usd_reference_kind::reference);
    EXPECT_EQ(entry->references[0].variant_set,  "modelVariant");
    EXPECT_EQ(entry->references[0].variant_name, "Plated");
}

TEST(Variant_arcs, unselected_variant_arc_is_counted)
{
    const std::shared_ptr<erhe::scene::Node> root   = std::make_shared<erhe::scene::Xform>("root");
    const erhe::usd::Usd_load_result         result = load(test_data_path("variant_arcs.usda"), root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const erhe::usd::Usd_variant_set* const set = find_set(result.data, "/Carrier", "modelVariant");
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->selected, "Plated");
    const erhe::usd::Usd_variant* const selected = find_variant(*set, "Plated");
    ASSERT_NE(selected, nullptr);
    ASSERT_EQ(selected->references.size(), 1u);
    EXPECT_EQ(selected->references[0].prim_path, "/Widget");
    const erhe::usd::Usd_variant* const unselected = find_variant(*set, "Barred");
    ASSERT_NE(unselected, nullptr);
    EXPECT_TRUE(unselected->references.empty());
    EXPECT_EQ(set->unsupported_opinion_count, 1u);
}

TEST(Variant_arcs, variant_def_below_a_carrier_is_in_the_tree)
{
    const std::shared_ptr<erhe::scene::Node> root   = std::make_shared<erhe::scene::Xform>("root");
    const erhe::usd::Usd_load_result         result = load(test_data_path("variant_arcs.usda"), root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    // The variant's own content, below the prim whose arcs supply the rest.
    erhe::Hierarchy* const carrier = find_child(*root.get(), "Carrier");
    ASSERT_NE(carrier, nullptr);
    erhe::Hierarchy* const extras = find_child(*carrier, "Extras");
    ASSERT_NE(extras, nullptr);
    EXPECT_NE(find_descendant(*extras, "marker"), nullptr);

    // The same for a prim that authors its arc itself and defs the scope in a
    // variant block - the shape of usd-wg full_assets/Teapot/Teapot_Materials.
    erhe::Hierarchy* const direct = find_child(*root.get(), "Direct");
    ASSERT_NE(direct, nullptr);
    EXPECT_NE(find_child(*direct, "Looks"), nullptr);
    const erhe::usd::Usd_prim_references* const direct_entry = find_references(result.data, "/Direct");
    ASSERT_NE(direct_entry, nullptr);
    ASSERT_EQ(direct_entry->references.size(), 1u);
    EXPECT_TRUE(direct_entry->references[0].variant_set.empty());
}

TEST(Variant_arcs, save_writes_the_arc_inside_the_variant_block)
{
    const std::filesystem::path source_path  = test_data_path("variant_arcs.usda");
    const std::filesystem::path written_path = temporary_path("variant_arcs_written.usda");
    const std::filesystem::path second_path  = temporary_path("variant_arcs_second.usda");

    const std::shared_ptr<erhe::scene::Node> root   = std::make_shared<erhe::scene::Xform>("root");
    const erhe::usd::Usd_load_result         result = load(source_path, root);
    ASSERT_TRUE(result.error.empty()) << result.error;

    // The target file sits beside the written file, so the arcs the writer
    // relativizes name it the way the source did.
    std::error_code error_code{};
    std::filesystem::copy_file(
        test_data_path("reftarget.usda"),
        written_path.parent_path() / "reftarget.usda",
        std::filesystem::copy_options::overwrite_existing,
        error_code
    );

    const erhe::usd::Usd_save_arguments save_arguments{
        .path         = written_path,
        .root_node    = root,
        // The copy beside the written file is what the arcs name, so the
        // writer spells them relative to it, as the source did.
        .references   = to_save_references(result.data, written_path),
        .variant_sets = to_save_variant_sets(result.data, written_path)
    };
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;

    const std::string written = read_file(written_path);
    // The arc is inside the block, not on the prim carrying the set.
    const std::size_t block = written.find("\"Plated\"");
    ASSERT_NE(block, std::string::npos);
    EXPECT_NE(written.find("references = @reftarget.usda@</Widget>", block), std::string::npos) << written;
    // The prim a variant defs is written back inside its block as well.
    EXPECT_NE(written.find("def Scope \"Extras\"", block), std::string::npos) << written;

    // Reload and save again: the file is a fixed point.
    const std::shared_ptr<erhe::scene::Node> reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_result         reloaded      = load(written_path, reloaded_root);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

    const erhe::usd::Usd_prim_references* const entry = find_references(reloaded.data, "/Carrier");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->references.size(), 1u);
    EXPECT_EQ(entry->references[0].variant_set, "modelVariant");
    erhe::Hierarchy* const reloaded_carrier = find_descendant(*reloaded_root.get(), "Carrier");
    ASSERT_NE(reloaded_carrier, nullptr);
    EXPECT_NE(find_child(*reloaded_carrier, "Extras"), nullptr);

    const erhe::usd::Usd_save_arguments second_save_arguments{
        .path         = second_path,
        .root_node    = reloaded_root,
        .references   = to_save_references(reloaded.data, written_path),
        .variant_sets = to_save_variant_sets(reloaded.data, written_path)
    };
    const erhe::usd::Usd_save_result second_save = erhe::usd::save_usda(second_save_arguments);
    ASSERT_TRUE(second_save.error.empty()) << second_save.error;
    EXPECT_EQ(read_file(second_path), read_file(written_path));
}

} // anonymous namespace
