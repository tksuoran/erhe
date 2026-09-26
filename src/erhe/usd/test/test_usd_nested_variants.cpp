// A `variantSet` a variant block itself declares (doc/erhe/usd_compatibility_design.md
// section 6, "Variant opinions a variant set does not carry"). Such a set is a
// set of the prim carrying the outer set, tabled beside it and naming the block
// it is declared in; its selection is the strongest of the carrier's
// arc-carried selection (section 2 C7), the enclosing block's own `variants`
// metadatum, the prim's, and the first block; and its blocks contribute only
// while the enclosing variant is the selected one.

#include "test_temporary_directory.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/gprim.hpp"
#include "erhe_scene/mesh.hpp"
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
    const std::filesystem::path directory = erhe_usd_test::process_temporary_directory() / "erhe_usd_nested_variant_tests";
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

// One set of the table, named the way a nested set is: by the prim, the
// enclosing block and its own name. Both enclosing names are empty for a set
// the prim declares itself.
[[nodiscard]] auto find_set(
    const erhe::usd::Usd_data& data,
    const std::string&         stage_path,
    const std::string&         enclosing_set_name,
    const std::string&         enclosing_variant_name,
    const std::string&         set_name
) -> const erhe::usd::Usd_variant_set*
{
    for (const erhe::usd::Usd_variant_set& set : data.variant_sets) {
        if (
            (set.stage_path             == stage_path) &&
            (set.enclosing_set_name     == enclosing_set_name) &&
            (set.enclosing_variant_name == enclosing_variant_name) &&
            (set.set_name               == set_name)
        ) {
            return &set;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_variant(const erhe::usd::Usd_variant_set& set, const std::string& name) -> const erhe::usd::Usd_variant*
{
    for (const erhe::usd::Usd_variant& variant : set.variants) {
        if (variant.name == name) {
            return &variant;
        }
    }
    return nullptr;
}

[[nodiscard]] auto binding_of(const erhe::usd::Usd_variant& variant, const std::string& relative_path) -> std::string
{
    for (const erhe::usd::Usd_variant_binding& binding : variant.bindings) {
        if (binding.relative_path == relative_path) {
            return binding.material_path;
        }
    }
    return {};
}

[[nodiscard]] auto material_at(
    const std::shared_ptr<erhe::scene::Node>& root,
    const std::string&                        path
) -> erhe::primitive::Material*
{
    erhe::Hierarchy* prim = erhe::find_by_path(*root.get(), path);
    if (prim == nullptr) {
        return nullptr;
    }
    return erhe::is<erhe::primitive::Material>(prim) ? static_cast<erhe::primitive::Material*>(prim) : nullptr;
}

[[nodiscard]] auto mesh_at(const std::shared_ptr<erhe::scene::Node>& root, const std::string& path) -> const erhe::scene::Mesh*
{
    erhe::Hierarchy* prim = erhe::find_by_path(*root.get(), path);
    if ((prim == nullptr) || !erhe::is<erhe::scene::Mesh>(prim)) {
        return nullptr;
    }
    return static_cast<const erhe::scene::Mesh*>(prim);
}

[[nodiscard]] auto bound_material(const erhe::scene::Mesh& mesh) -> erhe::primitive::Material*
{
    return mesh.get_primitives().empty() ? nullptr : mesh.get_primitives()[0].material.get();
}

// Whether the item at `path` is in the tree and active - what tells a hoisted
// prim of the selected branch from one of every other branch.
[[nodiscard]] auto is_active_at(const std::shared_ptr<erhe::scene::Node>& root, const std::string& path) -> bool
{
    erhe::Hierarchy* const prim = erhe::find_by_path(*root.get(), path);
    return (prim != nullptr) && prim->get_value(erhe::Item_base::active_property);
}

// What the editor does with the table it read: the same sets, each binding's
// material resolved to the item at that stage path, the enclosing block
// carried through so a save writes the set back where the file authored it.
[[nodiscard]] auto to_save_variant_sets(
    const erhe::usd::Usd_data&                data,
    const std::shared_ptr<erhe::scene::Node>& root
) -> std::vector<erhe::usd::Usd_save_variant_set>
{
    const auto material_of_path = [&data, &root](const std::string& stage_path) -> std::shared_ptr<const erhe::primitive::Material> {
        const erhe::primitive::Material* material = material_at(root, stage_path.substr(1));
        if (material == nullptr) {
            return {};
        }
        for (const std::shared_ptr<erhe::primitive::Material>& candidate : data.materials) {
            if (candidate.get() == material) {
                return candidate;
            }
        }
        return {};
    };
    std::vector<erhe::usd::Usd_save_variant_set> save_sets;
    for (const erhe::usd::Usd_variant_set& set : data.variant_sets) {
        erhe::usd::Usd_save_variant_set save_set{};
        save_set.item                   = set.prim;
        save_set.set_name               = set.set_name;
        save_set.enclosing_set_name     = set.enclosing_set_name;
        save_set.enclosing_variant_name = set.enclosing_variant_name;
        save_set.selected               = set.selected;
        for (const erhe::usd::Usd_variant& variant : set.variants) {
            erhe::usd::Usd_save_variant save_variant{};
            save_variant.name      = variant.name;
            save_variant.overrides = variant.overrides;
            for (const erhe::usd::Usd_variant_binding& binding : variant.bindings) {
                save_variant.bindings.push_back(
                    erhe::usd::Usd_save_variant_binding{
                        .relative_path = binding.relative_path,
                        .material      = material_of_path(binding.material_path)
                    }
                );
            }
            erhe::Hierarchy* const carrier = dynamic_cast<erhe::Hierarchy*>(set.prim.get());
            for (const erhe::usd::Usd_variant_prim& variant_prim : variant.prims) {
                erhe::Hierarchy* const item = (carrier != nullptr)
                    ? erhe::find_by_path(*carrier, variant_prim.relative_path)
                    : nullptr;
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
            save_set.variants.push_back(std::move(save_variant));
        }
        save_sets.push_back(std::move(save_set));
    }
    return save_sets;
}

class Nested_variant_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("nested_variants.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

// Every `variantSet` a variant block declares is tabled beside the set that
// carries the block, on the same prim, naming the block it came from.
TEST_F(Nested_variant_import, the_nested_sets_are_tabled_with_their_enclosing_block)
{
    const erhe::usd::Usd_variant_set* model = find_set(result.data, "/Teapot", "", "", "modelVariant");
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->selected, "Utah");

    const erhe::usd::Usd_variant_set* fancy_shading = find_set(result.data, "/Teapot", "modelVariant", "Fancy", "shadingVariant");
    const erhe::usd::Usd_variant_set* utah_shading  = find_set(result.data, "/Teapot", "modelVariant", "Utah",  "shadingVariant");
    ASSERT_NE(fancy_shading, nullptr);
    ASSERT_NE(utah_shading,  nullptr);
    // The prim carrying the outer set is the prim of the nested set too.
    EXPECT_EQ(utah_shading->prim.get(), model->prim.get());
    ASSERT_EQ(fancy_shading->variants.size(), 1u);
    ASSERT_EQ(utah_shading->variants.size(),  2u);

    const erhe::usd::Usd_variant* lime = find_variant(*utah_shading, "CeramicLimeGreen");
    ASSERT_NE(lime, nullptr);
    EXPECT_EQ(binding_of(*lime, "Geometry"), "/Teapot/Look_1");
}

// The selection of a nested set: the enclosing block's own `variants`
// metadatum, then the prim's, then the first block. The carried selection is
// the strongest and has its own test below.
TEST_F(Nested_variant_import, the_selection_falls_back_from_the_block_to_the_prim_to_the_first)
{
    const erhe::usd::Usd_variant_set* shading = find_set(result.data, "/Teapot", "modelVariant", "Utah", "shadingVariant");
    const erhe::usd::Usd_variant_set* trim    = find_set(result.data, "/Teapot", "modelVariant", "Utah", "trimVariant");
    const erhe::usd::Usd_variant_set* extra   = find_set(result.data, "/Teapot", "modelVariant", "Utah", "extraVariant");
    ASSERT_NE(shading, nullptr);
    ASSERT_NE(trim,    nullptr);
    ASSERT_NE(extra,   nullptr);
    // The block authors `shadingVariant`, the prim authors `trimVariant`, and
    // nobody authors `extraVariant`.
    EXPECT_EQ(shading->selected, "CeramicLimeGreen");
    EXPECT_EQ(trim->selected,    "Gold");
    EXPECT_EQ(extra->selected,   "First");
}

// A nested set's binding reaches the mesh only while its enclosing variant is
// the selected one: `Utah` is selected, so the material of its
// `CeramicLimeGreen` block binds and the one of the `Fancy` branch does not.
// The binding names `/Teapot/Look`, the name the Utah block gave its own
// material, and the tree calls that one `Look_1` because the `Fancy` block
// claimed `Look` first - which is what the branch resolves the path through.
TEST_F(Nested_variant_import, only_the_selected_branch_binds)
{
    const erhe::scene::Mesh* mesh = mesh_at(root, "Teapot/Geometry");
    ASSERT_NE(mesh, nullptr);
    erhe::primitive::Material* const utah_look  = material_at(root, "Teapot/Look_1");
    erhe::primitive::Material* const fancy_look = material_at(root, "Teapot/Look");
    ASSERT_NE(utah_look,  nullptr);
    ASSERT_NE(fancy_look, nullptr);
    EXPECT_EQ(bound_material(*mesh), utah_look);

    // The opinions of the same block reached the mesh too - the erhe custom
    // attribute, and the constant `primvars:displayColor` that is the one
    // color of the surface (Gprim.display_color).
    EXPECT_FALSE(mesh->get_value(erhe::scene::Mesh::shadow_cast_property));
    const std::optional<glm::vec3> display_color = mesh->read_local_value(erhe::scene::Gprim::display_color_property);
    ASSERT_TRUE(display_color.has_value());
    EXPECT_NEAR(display_color.value().x, 0.325f, 1e-6f);
    EXPECT_NEAR(display_color.value().y, 0.825f, 1e-6f);
    EXPECT_NEAR(display_color.value().z, 0.0f,   1e-6f);
}

// A `def` child of a nested block is hoisted to the prim like any other, and
// is active only when both the enclosing variant and the nested one are
// selected.
TEST_F(Nested_variant_import, the_prims_of_a_nested_block_are_hoisted_and_gated_by_both)
{
    // Both blocks of the selected branch's nested set.
    EXPECT_TRUE (is_active_at(root, "Teapot/GreenTrim"));
    EXPECT_FALSE(is_active_at(root, "Teapot/BlackTrim"));
    // The selected block of an unselected branch: its enclosing variant is
    // not the selection, so nothing of it is part of the composed prim.
    EXPECT_NE   (erhe::find_by_path(*root.get(), "Teapot/FancyTrim"), nullptr);
    EXPECT_FALSE(is_active_at(root, "Teapot/FancyTrim"));
    // A second nested set of the selected branch: its own first block is the
    // selection, so that block's prim is the active one.
    EXPECT_TRUE (is_active_at(root, "Teapot/FirstExtra"));
    EXPECT_FALSE(is_active_at(root, "Teapot/SecondExtra"));
}

// A selection a composition arc carries is stronger than the block's own and
// the prim's, and it reaches a nested set the same way it reaches a top-level
// one: the whole `Fancy` branch composes.
TEST(Nested_variant_carried_selection, an_arc_selects_the_outer_and_the_inner_set)
{
    const std::shared_ptr<erhe::scene::Node> carrier_root = std::make_shared<erhe::scene::Xform>("carrier_root");
    const erhe::usd::Usd_load_result         carrier      = load(test_data_path("nested_variants_carrier.usda"), carrier_root);
    ASSERT_TRUE(carrier.error.empty()) << carrier.error;

    // The read side of C7: the carrier reports both selections on its arc,
    // including the one naming a set only a variant block of the target
    // declares.
    ASSERT_EQ(carrier.data.references.size(), 1u);
    const std::vector<erhe::usd::Usd_reference>& references = carrier.data.references[0].references;
    ASSERT_EQ(references.size(), 1u);
    ASSERT_EQ(references[0].variant_selections.size(), 2u);

    erhe::usd::Usd_variant_selections selections{};
    selections.root_prim_path = "/Teapot";
    for (const erhe::usd::Usd_variant_selection& selection : references[0].variant_selections) {
        selections.entries.push_back(selection);
    }

    const std::shared_ptr<erhe::scene::Node> root   = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_result         result = load(test_data_path("nested_variants.usda"), root, selections);
    ASSERT_TRUE(result.error.empty()) << result.error;

    const erhe::usd::Usd_variant_set* model         = find_set(result.data, "/Teapot", "", "", "modelVariant");
    const erhe::usd::Usd_variant_set* fancy_shading = find_set(result.data, "/Teapot", "modelVariant", "Fancy", "shadingVariant");
    ASSERT_NE(model,         nullptr);
    ASSERT_NE(fancy_shading, nullptr);
    EXPECT_EQ(model->selected,         "Fancy");
    EXPECT_EQ(fancy_shading->selected, "PorcelainFlowers");

    const erhe::scene::Mesh* mesh = mesh_at(root, "Teapot/Geometry");
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(bound_material(*mesh), material_at(root, "Teapot/Look"));
    EXPECT_TRUE (is_active_at(root, "Teapot/FancyTrim"));
    EXPECT_FALSE(is_active_at(root, "Teapot/GreenTrim"));
}

class Nested_variant_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        source      = load(test_data_path("nested_variants.usda"), source_root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("nested_variants.usda");
        const erhe::usd::Usd_save_arguments save_arguments{
            .path         = written_path,
            .root_node    = source_root,
            .materials    = source.data.materials,
            .variant_sets = to_save_variant_sets(source.data, source_root)
        };
        const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        reloaded      = load(written_path, reloaded_root);
        ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    }

    std::shared_ptr<erhe::scene::Node> source_root;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    std::filesystem::path              written_path;
    erhe::usd::Usd_load_result         source;
    erhe::usd::Usd_load_result         reloaded;
};

// A nested set goes back inside the block that declared it, with its
// selection on that block rather than on the prim.
TEST_F(Nested_variant_round_trip, a_nested_set_is_written_inside_its_block)
{
    const std::string written = read_file(written_path);
    const std::size_t outer = written.find("variantSet \"modelVariant\" = {");
    const std::size_t inner = written.find("variantSet \"shadingVariant\" = {");
    ASSERT_NE(outer, std::string::npos) << written;
    ASSERT_NE(inner, std::string::npos) << written;
    EXPECT_GT(inner, outer) << written;
    EXPECT_NE(written.find("string shadingVariant = \"CeramicLimeGreen\""), std::string::npos) << written;
    EXPECT_NE(written.find("string modelVariant = \"Utah\""),               std::string::npos) << written;
    EXPECT_NE(written.find("rel material:binding = </Teapot/Look>"), std::string::npos) << written;
}

// The table the reload reads is the table that was written: every set with the
// block it is declared in, its selection and its bindings.
TEST_F(Nested_variant_round_trip, the_nested_table_comes_back_as_it_went_out)
{
    ASSERT_EQ(reloaded.data.variant_sets.size(), source.data.variant_sets.size());
    for (const erhe::usd::Usd_variant_set& before : source.data.variant_sets) {
        const erhe::usd::Usd_variant_set* after = find_set(
            reloaded.data, before.stage_path, before.enclosing_set_name, before.enclosing_variant_name, before.set_name
        );
        ASSERT_NE(after, nullptr) << before.set_name;
        EXPECT_EQ(after->selected, before.selected) << before.set_name;
        ASSERT_EQ(after->variants.size(), before.variants.size()) << before.set_name;
        for (std::size_t index = 0, end = before.variants.size(); index < end; ++index) {
            EXPECT_EQ(after->variants[index].name, before.variants[index].name);
            ASSERT_EQ(after->variants[index].bindings.size(), before.variants[index].bindings.size())
                << before.set_name << " " << before.variants[index].name;
        }
    }

    const erhe::scene::Mesh* mesh = mesh_at(reloaded_root, "Teapot/Geometry");
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(bound_material(*mesh), material_at(reloaded_root, "Teapot/Look_1"));
    EXPECT_TRUE (is_active_at(reloaded_root, "Teapot/GreenTrim"));
    EXPECT_FALSE(is_active_at(reloaded_root, "Teapot/FancyTrim"));
}

// Writing what was just read back reaches a fixed point.
TEST_F(Nested_variant_round_trip, second_save_is_byte_identical)
{
    const std::filesystem::path second_path = temporary_path("nested_variants_second.usda");
    const erhe::usd::Usd_save_arguments save_arguments{
        .path         = second_path,
        .root_node    = reloaded_root,
        .materials    = reloaded.data.materials,
        .variant_sets = to_save_variant_sets(reloaded.data, reloaded_root)
    };
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;
    EXPECT_EQ(read_file(second_path), read_file(written_path));
}


} // anonymous namespace
