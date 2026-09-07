// Material-binding variant sets (doc/usd-compatibility-plan.md X4). LightUSD
// composes nothing, so a variant contributes no opinion to the composed prim:
// the reader takes the `variantSet` blocks off the root layer's own prim
// specs, records what each variant binds, and applies the selected variant's
// bindings to the meshes itself. The writer puts the whole table back, so a
// stage that came in with a selection goes out with it.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
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
            save_variant.name = variant.name;
            for (const erhe::usd::Usd_variant_binding& binding : variant.bindings) {
                save_variant.bindings.push_back(
                    erhe::usd::Usd_save_variant_binding{
                        .relative_path = binding.relative_path,
                        .material      = material_of_path(binding.material_path)
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
    ASSERT_EQ(result.data.variant_sets.size(), 2u);

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

// A variant that authors anything but a material binding is the later slice
// (node subtree variants): the set says how much it left out, once.
TEST_F(Variant_import, a_non_binding_opinion_is_reported_once_for_the_set)
{
    const erhe::usd::Usd_variant_set* detail = find_set(result.data, "/World/Extra", "detail");
    ASSERT_NE(detail, nullptr);
    EXPECT_EQ(detail->selected, "low");
    EXPECT_EQ(detail->unsupported_opinion_count, 2u);
    for (const erhe::usd::Usd_variant& variant : detail->variants) {
        EXPECT_TRUE(variant.bindings.empty()) << variant.name;
    }
    EXPECT_NE(result.warning.find("variant set 'detail' authors 2 opinion(s)"), std::string::npos) << result.warning;
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
