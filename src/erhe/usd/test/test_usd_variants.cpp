// Variant sets (doc/usd-compatibility-plan.md X4). LightUSD composes nothing,
// so a variant contributes no opinion to the composed prim: the reader takes
// the `variantSet` blocks off the root layer's own prim specs, records what
// each variant binds and what it authors as property opinions, and applies
// the selected variant to the imported result itself. The writer puts the
// whole table back, so a stage that came in with a selection goes out with
// it.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_scene/instance_override.hpp"
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
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_variant_tests";
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

// The opinion `variant` authors for one relative path and property name, or
// an empty string when it authors none.
[[nodiscard]] auto opinion_of(
    const erhe::usd::Usd_variant& variant,
    const std::string&            relative_path,
    const std::string&            name
) -> std::string
{
    for (const erhe::scene::Instance_override& entry : variant.overrides) {
        if (entry.relative_path != relative_path) {
            continue;
        }
        for (const erhe::scene::Instance_override_value& value : entry.values) {
            if (value.name == name) {
                return value.text;
            }
        }
    }
    return {};
}

[[nodiscard]] auto override_of(
    const std::vector<erhe::scene::Instance_override>& overrides,
    const std::string&                                 relative_path
) -> const erhe::scene::Instance_override*
{
    for (const erhe::scene::Instance_override& entry : overrides) {
        if (entry.relative_path == relative_path) {
            return &entry;
        }
    }
    return nullptr;
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

// The material the mesh's primitive at `index` binds.
[[nodiscard]] auto material_bound_to_primitive(const erhe::scene::Mesh& mesh, const std::size_t index) -> erhe::primitive::Material*
{
    return (index < mesh.get_primitives().size()) ? mesh.get_primitives()[index].material.get() : nullptr;
}

// What the editor does with the table it read: the same sets, with each
// binding's material resolved to the item at that stage path (commit 2 of X4
// does it against the scene; a stage path is an item path below the root).
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
        save_set.item     = set.prim;
        save_set.set_name = set.set_name;
        save_set.selected = set.selected;
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

class Variant_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("variants.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Variant_import, the_sets_and_their_bindings_are_recorded)
{
    ASSERT_EQ(result.data.variant_sets.size(), 3u);

    const erhe::usd::Usd_variant_set* look = find_set(result.data, "/World/Holder", "look");
    ASSERT_NE(look, nullptr);
    EXPECT_EQ(look->selected, "blue");
    EXPECT_EQ(look->prim.get(), erhe::find_by_path(*root.get(), "World/Holder"));
    ASSERT_EQ(look->variants.size(), 2u);

    const erhe::usd::Usd_variant* blue = find_variant(*look, "blue");
    const erhe::usd::Usd_variant* red  = find_variant(*look, "red");
    ASSERT_NE(blue, nullptr);
    ASSERT_NE(red,  nullptr);
    EXPECT_EQ(binding_of(*blue, "quad"),      "/World/Looks/Blue");
    EXPECT_EQ(binding_of(*blue, "quad/half"), "/World/Looks/Red");
    EXPECT_EQ(binding_of(*red,  "quad"),      "/World/Looks/Red");
    EXPECT_EQ(binding_of(*red,  "quad/half"), "/World/Looks/Blue");
    EXPECT_EQ(look->unsupported_opinion_count, 0u);
}

TEST_F(Variant_import, the_selected_variant_is_what_the_meshes_bind)
{
    const erhe::scene::Mesh* mesh = mesh_at(root, "World/Holder/quad");
    ASSERT_NE(mesh, nullptr);
    ASSERT_EQ(mesh->get_primitives().size(), 2u);

    erhe::primitive::Material* red  = material_at(root, "World/Looks/Red");
    erhe::primitive::Material* blue = material_at(root, "World/Looks/Blue");
    ASSERT_NE(red, nullptr);
    // Nothing outside the variant binds `Blue`, so no binding of the composed
    // stage reaches it: it exists because the variant names it.
    ASSERT_NE(blue, nullptr);

    // The authored subset comes first, the facets no subset claims second.
    EXPECT_EQ(material_bound_to_primitive(*mesh, 0), red);
    EXPECT_EQ(material_bound_to_primitive(*mesh, 1), blue);
}

// The property opinions a variant authors are recorded the way an `over`
// below a reference carrier is: by the path below the prim carrying the set,
// an empty path being that prim itself.
TEST_F(Variant_import, the_property_opinions_are_recorded)
{
    const erhe::usd::Usd_variant_set* detail = find_set(result.data, "/World/Extra", "detail");
    ASSERT_NE(detail, nullptr);
    EXPECT_EQ(detail->selected, "low");
    EXPECT_EQ(detail->unsupported_opinion_count, 0u);

    const erhe::usd::Usd_variant* high = find_variant(*detail, "high");
    const erhe::usd::Usd_variant* low  = find_variant(*detail, "low");
    ASSERT_NE(high, nullptr);
    ASSERT_NE(low,  nullptr);

    // An attribute on the prim carrying the set.
    EXPECT_EQ(opinion_of(*high, "",     "visible"), "true");
    EXPECT_EQ(opinion_of(*low,  "",     "visible"), "false");
    // An attribute on an `over` child.
    EXPECT_EQ(opinion_of(*low,  "part", "visible"), "false");
    EXPECT_EQ(opinion_of(*high, "part", "visible"), "");

    // The xformOps of an `over` child.
    const erhe::scene::Instance_override* high_part = override_of(high->overrides, "part");
    ASSERT_NE(high_part, nullptr);
    EXPECT_TRUE(high_part->transform_overridden);
    EXPECT_FLOAT_EQ(high_part->transform[3][0], 3.0f);
    ASSERT_TRUE(high_part->xform_op_stack.has_value());
    ASSERT_EQ(high_part->xform_op_stack.value().ops.size(), 1u);

    // An `erhe:Owner:name` custom attribute of a deeper `over`.
    const erhe::usd::Usd_variant_set* look = find_set(result.data, "/World/Holder", "look");
    ASSERT_NE(look, nullptr);
    const erhe::usd::Usd_variant* red = find_variant(*look, "red");
    ASSERT_NE(red, nullptr);
    // A USD `bool` prints as 0 / 1, which is one of the two spellings the D16
    // parse accepts for a boolean.
    EXPECT_EQ(opinion_of(*red, "quad", "Mesh.shadow_cast"), "0");
    EXPECT_EQ(look->unsupported_opinion_count, 0u);
}

// The base values are what the prims held before the selected variant's
// opinions reached them, for every path and name any variant authors: what a
// switch to another variant restores.
TEST_F(Variant_import, the_base_values_are_captured)
{
    const erhe::usd::Usd_variant_set* detail = find_set(result.data, "/World/Extra", "detail");
    ASSERT_NE(detail, nullptr);

    const erhe::scene::Instance_override* base_root = override_of(detail->base_values, "");
    ASSERT_NE(base_root, nullptr);
    ASSERT_EQ(base_root->values.size(), 1u);
    EXPECT_EQ(base_root->values[0].name, "visible");
    // The stage authors no visibility on `/World/Extra` outside the variants.
    EXPECT_EQ(base_root->values[0].state, erhe::scene::Instance_override_value_state::cleared);

    const erhe::scene::Instance_override* base_part = override_of(detail->base_values, "part");
    ASSERT_NE(base_part, nullptr);
    EXPECT_TRUE(base_part->transform_overridden);
    EXPECT_FLOAT_EQ(base_part->transform[3][0], 0.0f);
}

// The selected variant's opinions are applied to the imported items, the way
// its bindings are.
TEST_F(Variant_import, the_selected_variant_opinions_are_applied)
{
    erhe::Hierarchy* extra = erhe::find_by_path(*root.get(), "World/Extra");
    erhe::Hierarchy* part  = erhe::find_by_path(*root.get(), "World/Extra/part");
    ASSERT_NE(extra, nullptr);
    ASSERT_NE(part,  nullptr);
    EXPECT_FALSE(extra->get_value(erhe::Item_base::visible_property));
    EXPECT_FALSE(part->get_value(erhe::Item_base::visible_property));

    // `blue` is selected and authors no shadow_cast, so the mesh keeps its own.
    const erhe::scene::Mesh* mesh = mesh_at(root, "World/Holder/quad");
    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(mesh->get_value(erhe::scene::Mesh::shadow_cast_property));
}

// A variant's `def` children are prims of the tree, whichever variant is
// selected: the loader hoists every variant's prims below the prim carrying
// the set and gives them sibling-unique names, so the two `tri` meshes the
// two variants of `shape` author are `tri` and `tri_1`
// (doc/usd-compatibility-plan.md X4).
TEST_F(Variant_import, the_prims_a_variant_adds_are_in_the_tree)
{
    const erhe::usd::Usd_variant_set* shape = find_set(result.data, "/World/Swap", "shape");
    ASSERT_NE(shape, nullptr);
    EXPECT_EQ(shape->selected, "first");
    EXPECT_EQ(shape->unsupported_opinion_count, 0u);

    const erhe::usd::Usd_variant* first  = find_variant(*shape, "first");
    const erhe::usd::Usd_variant* second = find_variant(*shape, "second");
    ASSERT_NE(first,  nullptr);
    ASSERT_NE(second, nullptr);

    ASSERT_EQ(first->prims.size(), 1u);
    EXPECT_EQ(first->prims[0].relative_path, "tri");
    EXPECT_EQ(first->prims[0].authored_name, "tri");

    // In the order the variant block authors them.
    ASSERT_EQ(second->prims.size(), 2u);
    // The name the file gave it is held by the other variant's prim, so the
    // tree gives it the next free one by the M2 rule.
    EXPECT_EQ(second->prims[0].relative_path, "tri_1");
    EXPECT_EQ(second->prims[0].authored_name, "tri");
    EXPECT_EQ(second->prims[1].relative_path, "group");
    EXPECT_EQ(second->prims[1].authored_name, "group");

    EXPECT_NE(mesh_at(root, "World/Swap/tri"),          nullptr);
    EXPECT_NE(mesh_at(root, "World/Swap/tri_1"),        nullptr);
    EXPECT_NE(erhe::find_by_path(*root.get(), "World/Swap/group/inner"), nullptr);
}

// The selected variant's prims are active and every other variant's are not,
// which is what prunes them from the render, the pick and the simulation.
TEST_F(Variant_import, only_the_selected_variants_prims_are_active)
{
    erhe::Hierarchy* const selected   = erhe::find_by_path(*root.get(), "World/Swap/tri");
    erhe::Hierarchy* const unselected = erhe::find_by_path(*root.get(), "World/Swap/tri_1");
    erhe::Hierarchy* const group      = erhe::find_by_path(*root.get(), "World/Swap/group");
    ASSERT_NE(selected,   nullptr);
    ASSERT_NE(unselected, nullptr);
    ASSERT_NE(group,      nullptr);
    EXPECT_TRUE (selected  ->get_value(erhe::Item_base::active_property));
    EXPECT_FALSE(unselected->get_value(erhe::Item_base::active_property));
    EXPECT_FALSE(group     ->get_value(erhe::Item_base::active_property));
    // The selected variant's prim holds no opinion of its own: it is active
    // because its variant is the selection.
    EXPECT_FALSE(selected->has_local_value(erhe::Item_base::active_property.get()));
}

class Variant_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        source      = load(test_data_path("variants.usda"), source_root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("variants.usda");
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

TEST_F(Variant_round_trip, the_variant_lines_are_written)
{
    const std::string written = read_file(written_path);
    EXPECT_NE(written.find("append variantSets = \"look\""),   std::string::npos) << written;
    EXPECT_NE(written.find("string look = \"blue\""),          std::string::npos) << written;
    EXPECT_NE(written.find("variantSet \"look\" = {"),         std::string::npos) << written;
    EXPECT_NE(written.find("\"blue\" {"),                      std::string::npos) << written;
    EXPECT_NE(written.find("\"red\" {"),                       std::string::npos) << written;
    EXPECT_NE(written.find("rel material:binding = </World/Looks/Blue>"), std::string::npos) << written;
    EXPECT_NE(written.find("rel material:binding = </World/Looks/Red>"),  std::string::npos) << written;
    // The property opinions of the variants, in the same form the reader reads
    // them back: the native tokens, the erhe custom attribute, the xformOps.
    EXPECT_NE(written.find("append variantSets = \"detail\""), std::string::npos) << written;
    EXPECT_NE(written.find("token visibility = \"invisible\""), std::string::npos) << written;
    EXPECT_NE(written.find("token visibility = \"inherited\""), std::string::npos) << written;
    EXPECT_NE(written.find("erhe:Mesh:shadow_cast"),             std::string::npos) << written;
    EXPECT_NE(written.find("xformOp:translate"),                 std::string::npos) << written;
}

// A prim a variant adds goes back inside that variant's block, under the name
// the file gave it, and is not among the plain children of the prim carrying
// the set.
TEST_F(Variant_round_trip, the_prims_a_variant_adds_are_written_inside_their_block)
{
    const std::string written = read_file(written_path);
    const std::size_t variant_set = written.find("variantSet \"shape\" = {");
    ASSERT_NE(variant_set, std::string::npos) << written;

    const std::size_t first_tri = written.find("def Mesh \"tri\"");
    ASSERT_NE(first_tri, std::string::npos) << written;
    const std::size_t second_tri = written.find("def Mesh \"tri\"", first_tri + 1);
    ASSERT_NE(second_tri, std::string::npos) << written;
    EXPECT_GT(first_tri,  variant_set) << written;
    EXPECT_GT(second_tri, variant_set) << written;
    // The name the tree gave the second one is the tree's, not the file's.
    EXPECT_EQ(written.find("\"tri_1\""), std::string::npos) << written;
    EXPECT_NE(written.find("def Xform \"group\""), std::string::npos) << written;
    EXPECT_NE(written.find("def Xform \"inner\""), std::string::npos) << written;
}

// The reloaded scene holds the same prims, with the same names and the same
// active states: what makes a save of a switched scene a fixed point.
TEST_F(Variant_round_trip, the_prims_come_back_as_they_went_out)
{
    const erhe::usd::Usd_variant_set* shape = find_set(reloaded.data, "/World/Swap", "shape");
    ASSERT_NE(shape, nullptr);
    EXPECT_EQ(shape->selected, "first");
    const erhe::usd::Usd_variant* first  = find_variant(*shape, "first");
    const erhe::usd::Usd_variant* second = find_variant(*shape, "second");
    ASSERT_NE(first,  nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(first->prims.size(),  1u);
    ASSERT_EQ(second->prims.size(), 2u);
    EXPECT_EQ(first->prims[0].relative_path,  "tri");
    EXPECT_EQ(second->prims[0].relative_path, "tri_1");
    EXPECT_EQ(second->prims[0].authored_name, "tri");

    erhe::Hierarchy* const selected   = erhe::find_by_path(*reloaded_root.get(), "World/Swap/tri");
    erhe::Hierarchy* const unselected = erhe::find_by_path(*reloaded_root.get(), "World/Swap/tri_1");
    ASSERT_NE(selected,   nullptr);
    ASSERT_NE(unselected, nullptr);
    EXPECT_TRUE (selected  ->get_value(erhe::Item_base::active_property));
    EXPECT_FALSE(unselected->get_value(erhe::Item_base::active_property));
}

// The opinions survive the round trip as values, and the selected variant is
// applied to the reloaded scene exactly as it was to the source.
TEST_F(Variant_round_trip, the_opinions_come_back_as_they_went_out)
{
    for (std::size_t index = 0, end = source.data.variant_sets.size(); index < end; ++index) {
        const erhe::usd::Usd_variant_set& before = source.data.variant_sets[index];
        const erhe::usd::Usd_variant_set& after  = reloaded.data.variant_sets[index];
        ASSERT_EQ(after.variants.size(), before.variants.size()) << before.set_name;
        for (std::size_t variant = 0, variant_end = before.variants.size(); variant < variant_end; ++variant) {
            const erhe::usd::Usd_variant& before_variant = before.variants[variant];
            const erhe::usd::Usd_variant& after_variant  = after.variants[variant];
            ASSERT_EQ(after_variant.overrides.size(), before_variant.overrides.size())
                << before.set_name << " " << before_variant.name;
            for (std::size_t entry = 0, entry_end = before_variant.overrides.size(); entry < entry_end; ++entry) {
                const erhe::scene::Instance_override& before_entry = before_variant.overrides[entry];
                const erhe::scene::Instance_override& after_entry  = after_variant.overrides[entry];
                EXPECT_EQ(after_entry.relative_path,        before_entry.relative_path);
                EXPECT_EQ(after_entry.transform_overridden, before_entry.transform_overridden);
                ASSERT_EQ(after_entry.values.size(),        before_entry.values.size()) << before_entry.relative_path;
                for (std::size_t value = 0, value_end = before_entry.values.size(); value < value_end; ++value) {
                    EXPECT_EQ(after_entry.values[value].name, before_entry.values[value].name);
                    EXPECT_EQ(after_entry.values[value].text, before_entry.values[value].text);
                }
            }
        }
    }

    erhe::Hierarchy* part = erhe::find_by_path(*reloaded_root.get(), "World/Extra/part");
    ASSERT_NE(part, nullptr);
    EXPECT_FALSE(part->get_value(erhe::Item_base::visible_property));
}

TEST_F(Variant_round_trip, the_table_comes_back_as_it_went_out)
{
    ASSERT_EQ(reloaded.data.variant_sets.size(), source.data.variant_sets.size());
    for (std::size_t index = 0, end = source.data.variant_sets.size(); index < end; ++index) {
        const erhe::usd::Usd_variant_set& before = source.data.variant_sets[index];
        const erhe::usd::Usd_variant_set& after  = reloaded.data.variant_sets[index];
        EXPECT_EQ(after.stage_path, before.stage_path);
        EXPECT_EQ(after.set_name,   before.set_name);
        EXPECT_EQ(after.selected,   before.selected);
        ASSERT_EQ(after.variants.size(), before.variants.size()) << before.set_name;
        for (std::size_t variant = 0, variant_end = before.variants.size(); variant < variant_end; ++variant) {
            EXPECT_EQ(after.variants[variant].name, before.variants[variant].name);
            ASSERT_EQ(after.variants[variant].bindings.size(), before.variants[variant].bindings.size())
                << before.set_name << " " << before.variants[variant].name;
            for (std::size_t binding = 0, binding_end = before.variants[variant].bindings.size(); binding < binding_end; ++binding) {
                EXPECT_EQ(after.variants[variant].bindings[binding].relative_path, before.variants[variant].bindings[binding].relative_path);
                EXPECT_EQ(after.variants[variant].bindings[binding].material_path, before.variants[variant].bindings[binding].material_path);
            }
        }
    }

    const erhe::scene::Mesh* mesh = mesh_at(reloaded_root, "World/Holder/quad");
    ASSERT_NE(mesh, nullptr);
    ASSERT_EQ(mesh->get_primitives().size(), 2u);
    EXPECT_EQ(material_bound_to_primitive(*mesh, 0), material_at(reloaded_root, "World/Looks/Red"));
    EXPECT_EQ(material_bound_to_primitive(*mesh, 1), material_at(reloaded_root, "World/Looks/Blue"));
}

// Writing what was just read back must reach a fixed point: the second file
// is the first one, byte for byte.
TEST_F(Variant_round_trip, second_save_is_byte_identical)
{
    const std::filesystem::path second_path = temporary_path("variants_second.usda");
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
