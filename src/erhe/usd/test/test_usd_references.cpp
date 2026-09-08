#include "erhe_geometry/geometry.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene/xform_op.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] auto reference_test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto find_node(const erhe::usd::Usd_data& data, const std::string& name) -> std::shared_ptr<erhe::scene::Node>
{
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        if (node && (node->get_name() == name)) {
            return node;
        }
    }
    return {};
}

class References_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments arguments{
            .path          = reference_test_data_path("references.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(arguments);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(References_import, load_succeeds)
{
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_FALSE(result.data.nodes.empty());
    EXPECT_EQ(result.data.default_prim, "World");
}

TEST_F(References_import, arcs_are_reported_in_authored_order)
{
    ASSERT_EQ(result.data.references.size(), 5u);

    const erhe::usd::Usd_prim_references& default_ref = result.data.references[0];
    EXPECT_EQ(default_ref.stage_path, "/World/DefaultRef");
    ASSERT_EQ(default_ref.references.size(), 1u);
    EXPECT_EQ(default_ref.references[0].asset_path, "cube.usda");
    EXPECT_TRUE(default_ref.references[0].prim_path.empty());
    EXPECT_EQ(default_ref.references[0].kind, erhe::usd::Usd_reference_kind::reference);

    const erhe::usd::Usd_prim_references& path_ref = result.data.references[1];
    EXPECT_EQ(path_ref.stage_path, "/World/PathRef");
    ASSERT_EQ(path_ref.references.size(), 1u);
    EXPECT_EQ(path_ref.references[0].asset_path, "reftarget.usda");
    EXPECT_EQ(path_ref.references[0].prim_path, "/Library/Gadget");

    const erhe::usd::Usd_prim_references& internal_ref = result.data.references[2];
    EXPECT_EQ(internal_ref.stage_path, "/World/InternalRef");
    ASSERT_EQ(internal_ref.references.size(), 1u);
    EXPECT_TRUE(internal_ref.references[0].asset_path.empty());
    EXPECT_EQ(internal_ref.references[0].prim_path, "/World/Source");

    // A list-edited `references` op: `prepend` before `append`.
    const erhe::usd::Usd_prim_references& multi_ref = result.data.references[3];
    EXPECT_EQ(multi_ref.stage_path, "/World/MultiRef");
    ASSERT_EQ(multi_ref.references.size(), 2u);
    EXPECT_EQ(multi_ref.references[0].prim_path, "/Library/Gadget");
    EXPECT_TRUE(multi_ref.references[1].prim_path.empty());
    EXPECT_EQ(multi_ref.references[1].asset_path, "reftarget.usda");

    // A payload is read as a reference.
    const erhe::usd::Usd_prim_references& payload_ref = result.data.references[4];
    EXPECT_EQ(payload_ref.stage_path, "/World/PayloadRef");
    ASSERT_EQ(payload_ref.references.size(), 1u);
    EXPECT_EQ(payload_ref.references[0].kind, erhe::usd::Usd_reference_kind::payload);
    EXPECT_EQ(payload_ref.references[0].asset_path, "reftarget.usda");
    EXPECT_EQ(payload_ref.references[0].prim_path, "/Library/Gadget");
}

TEST_F(References_import, carrier_prims_are_imported)
{
    for (const char* name : {"DefaultRef", "PathRef", "InternalRef", "MultiRef", "PayloadRef"}) {
        EXPECT_TRUE(find_node(result.data, name).operator bool()) << name;
    }
}

TEST_F(References_import, carriers_are_the_reported_items)
{
    for (const erhe::usd::Usd_prim_references& entry : result.data.references) {
        ASSERT_TRUE(entry.item.operator bool()) << entry.stage_path;
    }
    EXPECT_EQ(result.data.references[0].item, find_node(result.data, "DefaultRef"));
}

TEST_F(References_import, referenced_prims_are_not_imported)
{
    // The arcs' targets supply them: the caller instantiates each target under
    // the carrier prim.
    for (const char* name : {"cube", "cam", "sun", "bar", "plate", "Gadget"}) {
        EXPECT_FALSE(find_node(result.data, name).operator bool()) << name;
    }
    // The internal reference's target is authored in this layer and imports as
    // the ordinary prim it is.
    EXPECT_TRUE(find_node(result.data, "quad").operator bool());

    const std::shared_ptr<erhe::scene::Node> carrier = find_node(result.data, "DefaultRef");
    ASSERT_TRUE(carrier.operator bool());
    EXPECT_TRUE(carrier->get_children().empty());
}

TEST_F(References_import, carrier_keeps_its_own_transform)
{
    const std::shared_ptr<erhe::scene::Node> carrier = find_node(result.data, "DefaultRef");
    ASSERT_TRUE(carrier.operator bool());
    const glm::vec3 translation = glm::vec3{carrier->parent_from_node_transform().get_matrix()[3]};
    EXPECT_FLOAT_EQ(translation.x, 5.0f);
}

// The arc targets load as ordinary files: this is what the prefab library
// reads to build the template a carrier instantiates.
TEST(Reference_target_import, target_file_loads)
{
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments arguments{
        .path          = reference_test_data_path("reftarget.usda"),
        .root_node     = root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result result = erhe::usd::load_usd(arguments);
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.data.default_prim, "Widget");
    EXPECT_TRUE(result.data.references.empty());
    EXPECT_TRUE(find_node(result.data, "Widget").operator bool());
    EXPECT_TRUE(find_node(result.data, "Gadget").operator bool());
    EXPECT_TRUE(find_node(result.data, "bar").operator bool());
}

// ---------------------------------------------------------------------------
// Export (doc/usd-compatibility-plan.md X1, save side)
// ---------------------------------------------------------------------------

[[nodiscard]] auto reference_temporary_directory() -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_reference_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory;
}

// A prim of a hand-built scene: content, so the writer plans it.
[[nodiscard]] auto make_prim(const char* name) -> std::shared_ptr<erhe::scene::Xform>
{
    std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>(name);
    node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible);
    return node;
}

// A prim instantiation put under a carrier: it names its counterpart in the
// template, the way attach_prefab_instance links a clone
// (doc/property-system.md D33). That link is what tells instance content
// from a prim the user parented under the carrier by hand.
[[nodiscard]] auto make_instance_content_prim(const char* name, const std::shared_ptr<erhe::Item_base>& counterpart) -> std::shared_ptr<erhe::scene::Xform>
{
    std::shared_ptr<erhe::scene::Xform> node = make_prim(name);
    node->set_reference(counterpart);
    return node;
}

// One template prim, standing in for the item a reference target supplies.
[[nodiscard]] auto make_template_prim(const char* name) -> std::shared_ptr<erhe::scene::Xform>
{
    return std::make_shared<erhe::scene::Xform>(name);
}

// The override entry at one relative path, or null.
[[nodiscard]] auto find_override(
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

// The D16 text of one override value, or null.
[[nodiscard]] auto find_override_value(const erhe::scene::Instance_override& entry, const std::string& name) -> const std::string*
{
    for (const erhe::scene::Instance_override_value& value : entry.values) {
        if (value.name == name) {
            return &value.text;
        }
    }
    return nullptr;
}

[[nodiscard]] auto read_text_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream     stream{path};
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// Write one hand-built scene and read the written file back.
class Reference_export
{
public:
    Reference_export(
        const std::shared_ptr<erhe::scene::Node>&               root,
        const std::vector<erhe::usd::Usd_save_prim_references>& references,
        const char*                                             written_file_name
    )
    {
        written_path = reference_temporary_directory() / written_file_name;
        const erhe::usd::Usd_save_arguments save_arguments{
            .path       = written_path,
            .root_node  = root,
            .references = references
        };
        save = erhe::usd::save_usda(save_arguments);

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = written_path,
            .root_node     = reloaded_root,
            .mesh_layer_id = 0
        };
        reloaded = erhe::usd::load_usd(load_arguments);
    }

    std::filesystem::path              written_path;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    erhe::usd::Usd_save_result         save;
    erhe::usd::Usd_load_result         reloaded;
};

TEST(References_export, two_arcs_are_written_in_order_without_instance_content)
{
    const std::shared_ptr<erhe::scene::Node> root    = std::make_shared<erhe::scene::Xform>("export_root");
    const std::shared_ptr<erhe::scene::Node> carrier = make_prim("Carrier");
    carrier->set_parent(root);
    const std::shared_ptr<erhe::scene::Xform> counterpart = make_template_prim("instance_content");
    make_instance_content_prim("instance_content", counterpart)->set_parent(carrier);

    const std::filesystem::path directory = reference_temporary_directory();
    const std::vector<erhe::usd::Usd_save_prim_references> references{
        erhe::usd::Usd_save_prim_references{
            .item       = carrier,
            .references = {
                erhe::usd::Usd_save_reference{.source_path = directory / "widget.usda", .prim_path = "/Library/Gadget"},
                erhe::usd::Usd_save_reference{.source_path = directory / "widget.usda", .prim_path = {}}
            }
        }
    };

    const Reference_export exported{root, references, "two_arcs.usda"};
    EXPECT_TRUE(exported.save.error.empty()) << exported.save.error;
    EXPECT_TRUE(exported.save.warning.empty()) << exported.save.warning;
    ASSERT_TRUE(exported.reloaded.error.empty()) << exported.reloaded.error;

    ASSERT_EQ(exported.reloaded.data.references.size(), 1u);
    const erhe::usd::Usd_prim_references& entry = exported.reloaded.data.references[0];
    EXPECT_EQ(entry.stage_path, "/Carrier");
    ASSERT_EQ(entry.references.size(), 2u);
    EXPECT_EQ(entry.references[0].asset_path, "widget.usda");
    EXPECT_EQ(entry.references[0].prim_path, "/Library/Gadget");
    EXPECT_EQ(entry.references[0].kind, erhe::usd::Usd_reference_kind::reference);
    EXPECT_EQ(entry.references[1].asset_path, "widget.usda");
    EXPECT_TRUE(entry.references[1].prim_path.empty());

    // The instance content is not written: the arcs' targets supply it.
    EXPECT_FALSE(find_node(exported.reloaded.data, "instance_content").operator bool());
    const std::shared_ptr<erhe::scene::Node> reloaded_carrier = find_node(exported.reloaded.data, "Carrier");
    ASSERT_TRUE(reloaded_carrier.operator bool());
    EXPECT_TRUE(reloaded_carrier->get_children().empty());
}

TEST(References_export, a_payload_arc_is_written_as_a_payload)
{
    const std::shared_ptr<erhe::scene::Node> root    = std::make_shared<erhe::scene::Xform>("export_root");
    const std::shared_ptr<erhe::scene::Node> carrier = make_prim("PayloadCarrier");
    carrier->set_parent(root);

    const std::vector<erhe::usd::Usd_save_prim_references> references{
        erhe::usd::Usd_save_prim_references{
            .item       = carrier,
            .references = {
                erhe::usd::Usd_save_reference{
                    .source_path = reference_temporary_directory() / "widget.usda",
                    .prim_path   = "/Library/Gadget",
                    .kind        = erhe::usd::Usd_reference_kind::payload
                }
            }
        }
    };

    const Reference_export exported{root, references, "payload_arc.usda"};
    EXPECT_TRUE(exported.save.error.empty()) << exported.save.error;
    ASSERT_TRUE(exported.reloaded.error.empty()) << exported.reloaded.error;
    ASSERT_EQ(exported.reloaded.data.references.size(), 1u);
    ASSERT_EQ(exported.reloaded.data.references[0].references.size(), 1u);
    EXPECT_EQ(exported.reloaded.data.references[0].references[0].kind, erhe::usd::Usd_reference_kind::payload);
    EXPECT_EQ(exported.reloaded.data.references[0].references[0].asset_path, "widget.usda");
    EXPECT_EQ(exported.reloaded.data.references[0].references[0].prim_path, "/Library/Gadget");
}

TEST(References_export, an_arc_into_the_written_file_is_internal)
{
    const std::shared_ptr<erhe::scene::Node> root    = std::make_shared<erhe::scene::Xform>("export_root");
    const std::shared_ptr<erhe::scene::Node> source  = make_prim("Source");
    const std::shared_ptr<erhe::scene::Node> carrier = make_prim("InternalCarrier");
    source->set_parent(root);
    carrier->set_parent(root);

    const std::vector<erhe::usd::Usd_save_prim_references> references{
        erhe::usd::Usd_save_prim_references{
            .item       = carrier,
            .references = {
                erhe::usd::Usd_save_reference{
                    .source_path = reference_temporary_directory() / "internal_arc.usda",
                    .prim_path   = "/World/Source"
                }
            }
        }
    };

    const Reference_export exported{root, references, "internal_arc.usda"};
    EXPECT_TRUE(exported.save.error.empty()) << exported.save.error;
    ASSERT_TRUE(exported.reloaded.error.empty()) << exported.reloaded.error;
    ASSERT_EQ(exported.reloaded.data.references.size(), 1u);
    ASSERT_EQ(exported.reloaded.data.references[0].references.size(), 1u);
    EXPECT_TRUE(exported.reloaded.data.references[0].references[0].asset_path.empty());
    EXPECT_EQ(exported.reloaded.data.references[0].references[0].prim_path, "/World/Source");
}

TEST(References_export, a_prim_parented_under_a_carrier_is_reported_and_left_out)
{
    const std::shared_ptr<erhe::scene::Node> root    = std::make_shared<erhe::scene::Xform>("export_root");
    const std::shared_ptr<erhe::scene::Node> carrier = make_prim("StrayCarrier");
    carrier->set_parent(root);
    const std::shared_ptr<erhe::scene::Xform> counterpart = make_template_prim("instance_content");
    make_instance_content_prim("instance_content", counterpart)->set_parent(carrier);
    make_prim("stray")->set_parent(carrier);

    const std::vector<erhe::usd::Usd_save_prim_references> references{
        erhe::usd::Usd_save_prim_references{
            .item       = carrier,
            .references = {
                erhe::usd::Usd_save_reference{.source_path = reference_temporary_directory() / "widget.usda"}
            }
        }
    };

    const Reference_export exported{root, references, "stray_child.usda"};
    EXPECT_TRUE(exported.save.error.empty()) << exported.save.error;
    EXPECT_NE(exported.save.warning.find("stray"), std::string::npos) << exported.save.warning;
    EXPECT_EQ(exported.save.warning.find("instance_content"), std::string::npos) << exported.save.warning;
    ASSERT_TRUE(exported.reloaded.error.empty()) << exported.reloaded.error;
    EXPECT_FALSE(find_node(exported.reloaded.data, "stray").operator bool());
}

// The round trip is a fixed point: the fixture's arcs are what the save
// writes, and reading the saved file back gives the same arcs again.
TEST(References_export, the_fixture_round_trips_to_the_same_arcs)
{
    const std::filesystem::path directory = reference_temporary_directory();
    for (const char* file_name : {"references.usda", "reftarget.usda", "cube.usda"}) {
        std::error_code error_code{};
        std::filesystem::copy_file(
            reference_test_data_path(file_name),
            directory / file_name,
            std::filesystem::copy_options::overwrite_existing,
            error_code
        );
        ASSERT_FALSE(static_cast<bool>(error_code)) << file_name << ": " << error_code.message();
    }
    const std::filesystem::path stage_path = directory / "references.usda";

    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments load_arguments{
        .path          = stage_path,
        .root_node     = root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result loaded = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    ASSERT_EQ(loaded.data.references.size(), 5u);

    // What the editor does with the arcs it read: the target file each arc
    // names, and the arc's prim path and form.
    std::vector<erhe::usd::Usd_save_prim_references> save_references;
    for (const erhe::usd::Usd_prim_references& entry : loaded.data.references) {
        erhe::usd::Usd_save_prim_references save_entry{};
        save_entry.item = entry.item;
        for (const erhe::usd::Usd_reference& reference : entry.references) {
            save_entry.references.push_back(
                erhe::usd::Usd_save_reference{
                    .source_path = reference.asset_path.empty() ? stage_path : (directory / reference.asset_path),
                    .prim_path   = reference.prim_path,
                    .kind        = reference.kind
                }
            );
        }
        save_references.push_back(std::move(save_entry));
    }

    // Written over the file it was read from, so an arc into that file stays
    // internal and every relative asset path is spelled as it was authored.
    const erhe::usd::Usd_save_arguments save_arguments{
        .path       = stage_path,
        .root_node  = root,
        .references = save_references
    };
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;

    const std::shared_ptr<erhe::scene::Node> reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_arguments reload_arguments{
        .path          = stage_path,
        .root_node     = reloaded_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(reload_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

    ASSERT_EQ(reloaded.data.references.size(), loaded.data.references.size());
    for (std::size_t index = 0, end = loaded.data.references.size(); index < end; ++index) {
        const erhe::usd::Usd_prim_references& before = loaded.data.references[index];
        const erhe::usd::Usd_prim_references& after  = reloaded.data.references[index];
        EXPECT_EQ(after.stage_path, before.stage_path);
        ASSERT_EQ(after.references.size(), before.references.size()) << before.stage_path;
        for (std::size_t arc = 0, arc_end = before.references.size(); arc < arc_end; ++arc) {
            EXPECT_EQ(after.references[arc].asset_path, before.references[arc].asset_path) << before.stage_path;
            EXPECT_EQ(after.references[arc].prim_path,  before.references[arc].prim_path)  << before.stage_path;
            EXPECT_EQ(after.references[arc].kind,       before.references[arc].kind)       << before.stage_path;
        }
    }

    // The instance content stays where it belongs: in the arcs' targets.
    for (const char* name : {"DefaultRef", "PathRef", "InternalRef", "MultiRef", "PayloadRef"}) {
        const std::shared_ptr<erhe::scene::Node> carrier = find_node(reloaded.data, name);
        ASSERT_TRUE(carrier.operator bool()) << name;
        EXPECT_TRUE(carrier->get_children().empty()) << name;
    }
    EXPECT_TRUE(find_node(reloaded.data, "quad").operator bool());
}

// ---------------------------------------------------------------------------
// Sparse overrides (doc/usd-compatibility-plan.md X2)
// ---------------------------------------------------------------------------

class Override_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments arguments{
            .path          = reference_test_data_path("references_override.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(arguments);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Override_import, an_over_below_a_carrier_is_read_as_an_override)
{
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.references.size(), 2u);
    const erhe::usd::Usd_prim_references& carrier = result.data.references[0];
    EXPECT_EQ(carrier.stage_path, "/World/Carrier");
    ASSERT_EQ(carrier.overrides.size(), 3u);

    const erhe::scene::Instance_override* arm = find_override(carrier.overrides, "arm");
    ASSERT_NE(arm, nullptr);
    const std::string* visible = find_override_value(*arm, "visible");
    ASSERT_NE(visible, nullptr);
    EXPECT_EQ(*visible, "false");
    EXPECT_TRUE(arm->transform_overridden);
    EXPECT_FLOAT_EQ(arm->transform[3][0], 1.0f);
    EXPECT_FLOAT_EQ(arm->transform[3][1], 2.0f);
    EXPECT_FLOAT_EQ(arm->transform[3][2], 3.0f);
    ASSERT_TRUE(arm->xform_op_stack.has_value());
    ASSERT_EQ(arm->xform_op_stack.value().ops.size(), 1u);
    EXPECT_EQ(arm->xform_op_stack.value().ops[0].type, erhe::scene::Xform_op_type::translate);
}

TEST_F(Override_import, a_nested_over_keeps_its_path_and_carries_active)
{
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.references.size(), 2u);
    const erhe::scene::Instance_override* plate = find_override(result.data.references[0].overrides, "arm/plate");
    ASSERT_NE(plate, nullptr);
    const std::string* active = find_override_value(*plate, "active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(*active, "false");
    const std::string* shadow_cast = find_override_value(*plate, "Mesh.shadow_cast");
    ASSERT_NE(shadow_cast, nullptr);
    EXPECT_EQ(*shadow_cast, "0"); // the USDA literal of a bool, which parse_value accepts
    EXPECT_FALSE(plate->transform_overridden);
}

// A reference protects its structure (plan section 5): a `def` below a
// A schema-named value of a resource inside the instance: `roughness` is a
// `Material` schema attribute on a `Material` prim, and an `over` has no
// schema, so it travels as the `erhe:Owner:name` custom attribute the reader
// reads back (doc/usd-compatibility-plan.md X2).
TEST_F(Override_import, an_over_on_a_material_is_read_as_an_override)
{
    ASSERT_FALSE(result.data.references.empty());
    const erhe::scene::Instance_override* look = find_override(result.data.references[0].overrides, "Look");
    ASSERT_NE(look, nullptr);
    const std::string* roughness = find_override_value(*look, "Material.roughness");
    ASSERT_NE(roughness, nullptr);
    EXPECT_EQ(*roughness, "0.25 0.25");
}

// referencing prim adds a prim to the reference, which is not an override.
TEST_F(Override_import, a_def_below_a_carrier_is_not_an_override)
{
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.references.size(), 2u);
    EXPECT_EQ(result.data.references[1].stage_path, "/World/DefCarrier");
    EXPECT_TRUE(result.data.references[1].overrides.empty());
}

// One carrier holding one instance, whose interior items name their template
// counterparts and hold the overrides the writer is to author.
class Override_scene final
{
public:
    Override_scene()
    {
        root = std::make_shared<erhe::scene::Xform>("export_root");
        carrier = make_prim("Carrier");
        carrier->set_parent(root);

        template_widget = make_template_prim("Widget");
        template_arm    = make_template_prim("arm");
        template_plate  = make_template_prim("plate");

        clone_widget = make_instance_content_prim("Widget", template_widget);
        clone_arm    = make_instance_content_prim("arm",    template_arm);
        clone_plate  = make_instance_content_prim("plate",  template_plate);
        clone_widget->set_parent(carrier);
        clone_arm->set_parent(clone_widget);
        clone_plate->set_parent(clone_arm);

        clone_arm->set_value(erhe::Item_base::visible_property, false);
        // The transform an imported override arrives with: an authored
        // xformOp stack (doc/usd-compatibility-plan.md M8), which the writer
        // is to author back as the ops it was given.
        erhe::scene::Xform_op_stack stack{};
        erhe::scene::Xform_op       translate_op{};
        translate_op.type      = erhe::scene::Xform_op_type::translate;
        translate_op.precision = erhe::scene::Xform_op_precision::double_;
        translate_op.value     = glm::dvec3{1.0, 2.0, 3.0};
        stack.ops.push_back(translate_op);
        clone_arm->set_xform_op_stack(stack);
        clone_plate->set_value(erhe::Item_base::active_property, false);

        // A resource prim inside the instance whose overridden value is one
        // the Material schema names: it has to reach the file too, and an
        // `over` has no schema to put it in.
        template_look = std::make_shared<erhe::primitive::Material>("Look");
        clone_look    = std::make_shared<erhe::primitive::Material>("Look");
        clone_look->enable_flag_bits(erhe::Item_flags::show_in_ui);
        clone_look->set_reference(template_look);
        clone_look->set_parent(clone_widget);
        clone_look->set_value(erhe::primitive::Material::roughness_property, glm::vec2{0.25f, 0.25f});
    }

    std::shared_ptr<erhe::scene::Node>  root;
    std::shared_ptr<erhe::scene::Node>  carrier;
    std::shared_ptr<erhe::scene::Xform> template_widget;
    std::shared_ptr<erhe::scene::Xform> template_arm;
    std::shared_ptr<erhe::scene::Xform> template_plate;
    std::shared_ptr<erhe::scene::Xform> clone_widget;
    std::shared_ptr<erhe::scene::Xform> clone_arm;
    std::shared_ptr<erhe::scene::Xform> clone_plate;
    std::shared_ptr<erhe::primitive::Material> template_look;
    std::shared_ptr<erhe::primitive::Material> clone_look;
};

[[nodiscard]] auto override_save_references(const std::shared_ptr<erhe::scene::Node>& carrier) -> std::vector<erhe::usd::Usd_save_prim_references>
{
    return std::vector<erhe::usd::Usd_save_prim_references>{
        erhe::usd::Usd_save_prim_references{
            .item       = carrier,
            .references = {
                erhe::usd::Usd_save_reference{.source_path = reference_temporary_directory() / "override_widget.usda"}
            }
        }
    };
}

TEST(Override_export, overrides_are_written_as_over_prims)
{
    const Override_scene   scene;
    const Reference_export exported{scene.root, override_save_references(scene.carrier), "written_overrides.usda"};
    EXPECT_TRUE(exported.save.error.empty()) << exported.save.error;
    EXPECT_TRUE(exported.save.warning.empty()) << exported.save.warning;

    const std::string written = read_text_file(exported.written_path);
    EXPECT_NE(written.find("over \"arm\""), std::string::npos) << written;
    EXPECT_NE(written.find("over \"plate\""), std::string::npos) << written;
    EXPECT_NE(written.find("token visibility = \"invisible\""), std::string::npos) << written;
    EXPECT_NE(written.find("active = false"), std::string::npos) << written;
    EXPECT_NE(written.find("xformOp:translate"), std::string::npos) << written;
    // A Material schema attribute of an item inside the instance: an `over`
    // carries no schema, so the value travels as the custom attribute form.
    EXPECT_NE(written.find("over \"Look\""), std::string::npos) << written;
    EXPECT_NE(written.find("erhe:Material:roughness"), std::string::npos) << written;
    // The instance content itself is not written: the arc's target supplies it.
    EXPECT_EQ(written.find("def Xform \"arm\""), std::string::npos) << written;
}

TEST(Override_export, written_overrides_read_back_the_same)
{
    const Override_scene   scene;
    const Reference_export exported{scene.root, override_save_references(scene.carrier), "written_overrides_reload.usda"};
    ASSERT_TRUE(exported.reloaded.error.empty()) << exported.reloaded.error;
    ASSERT_EQ(exported.reloaded.data.references.size(), 1u);
    const std::vector<erhe::scene::Instance_override>& overrides = exported.reloaded.data.references[0].overrides;
    ASSERT_EQ(overrides.size(), 3u);

    const erhe::scene::Instance_override* look = find_override(overrides, "Look");
    ASSERT_NE(look, nullptr);
    const std::string* roughness = find_override_value(*look, "Material.roughness");
    ASSERT_NE(roughness, nullptr);
    EXPECT_EQ(*roughness, "0.25 0.25");

    const erhe::scene::Instance_override* arm = find_override(overrides, "arm");
    ASSERT_NE(arm, nullptr);
    const std::string* visible = find_override_value(*arm, "visible");
    ASSERT_NE(visible, nullptr);
    EXPECT_EQ(*visible, "false");
    EXPECT_TRUE(arm->transform_overridden);
    EXPECT_FLOAT_EQ(arm->transform[3][0], 1.0f);

    const erhe::scene::Instance_override* plate = find_override(overrides, "arm/plate");
    ASSERT_NE(plate, nullptr);
    const std::string* active = find_override_value(*plate, "active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(*active, "false");
}

// ---------------------------------------------------------------------------
// A material binding authored as an override (doc/usd-compatibility-plan.md
// X2): the binding of a mesh inside an instance, and the binding of one group
// of that mesh's facets.
// ---------------------------------------------------------------------------

class Binding_override_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments arguments{
            .path          = reference_test_data_path("references_binding_override.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(arguments);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Binding_override_import, an_over_that_binds_a_material_carries_the_binding)
{
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.references.size(), 2u);
    EXPECT_EQ(result.data.references[0].stage_path, "/World/Carrier");
    const erhe::scene::Instance_override* plate = find_override(result.data.references[0].overrides, "arm/plate");
    ASSERT_NE(plate, nullptr);
    EXPECT_EQ(plate->material_path, "/World/materials/grey");
    EXPECT_TRUE(plate->values.empty());
}

// A GeomSubset is a prim below its mesh, so the group of facets the binding
// covers is the last name of the override's path.
TEST_F(Binding_override_import, a_binding_on_a_subset_names_the_group_in_its_path)
{
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.references.size(), 2u);
    EXPECT_EQ(result.data.references[1].stage_path, "/World/SubsetCarrier");
    const erhe::scene::Instance_override* front = find_override(result.data.references[1].overrides, "arm/plate/front");
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->material_path, "/World/materials/red");
    EXPECT_TRUE(front->values.empty());
    EXPECT_FALSE(front->transform_overridden);
}

// One carrier holding one instance whose mesh rebinds a material: with one
// group of facets the binding is the mesh's own, and with two it covers one
// group.
class Binding_override_scene final
{
public:
    explicit Binding_override_scene(const std::size_t primitive_count)
    {
        root    = std::make_shared<erhe::scene::Xform>("export_root");
        carrier = make_prim("Carrier");
        carrier->set_parent(root);

        grey = std::make_shared<erhe::primitive::Material>("grey");
        grey->enable_flag_bits(erhe::Item_flags::show_in_ui);
        grey->set_parent(root);
        base = std::make_shared<erhe::primitive::Material>("base");

        template_widget = make_template_prim("Widget");
        clone_widget    = make_instance_content_prim("Widget", template_widget);
        clone_widget->set_parent(carrier);

        template_plate = std::make_shared<erhe::scene::Mesh>("plate");
        clone_plate    = std::make_shared<erhe::scene::Mesh>("plate");
        for (std::size_t index = 0; index < primitive_count; ++index) {
            const std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>(
                (primitive_count == 1) ? "plate" : ((index == 0) ? "plate.front" : "plate.back")
            );
            template_plate->add_primitive(std::make_shared<erhe::primitive::Primitive>(geometry), base);
            clone_plate->add_primitive(std::make_shared<erhe::primitive::Primitive>(geometry), base);
        }
        clone_plate->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        clone_plate->set_reference(template_plate);
        clone_plate->set_parent(clone_widget);
        // The one thing this instance overrides: the material of its last
        // group of facets.
        clone_plate->set_primitive_material(primitive_count - 1, grey);
    }

    std::shared_ptr<erhe::scene::Node>         root;
    std::shared_ptr<erhe::scene::Node>         carrier;
    std::shared_ptr<erhe::scene::Xform>        template_widget;
    std::shared_ptr<erhe::scene::Xform>        clone_widget;
    std::shared_ptr<erhe::scene::Mesh>         template_plate;
    std::shared_ptr<erhe::scene::Mesh>         clone_plate;
    std::shared_ptr<erhe::primitive::Material> grey;
    std::shared_ptr<erhe::primitive::Material> base;
};

[[nodiscard]] auto binding_save_references(const std::shared_ptr<erhe::scene::Node>& carrier) -> std::vector<erhe::usd::Usd_save_prim_references>
{
    return std::vector<erhe::usd::Usd_save_prim_references>{
        erhe::usd::Usd_save_prim_references{
            .item       = carrier,
            .references = {
                erhe::usd::Usd_save_reference{.source_path = reference_temporary_directory() / "binding_widget.usda"}
            }
        }
    };
}

TEST(Binding_override_export, a_rebound_mesh_writes_the_relationship_and_the_api_schema)
{
    const Binding_override_scene scene{1};
    const Reference_export       exported{scene.root, binding_save_references(scene.carrier), "written_binding_override.usda"};
    EXPECT_TRUE(exported.save.error.empty()) << exported.save.error;
    EXPECT_TRUE(exported.save.warning.empty()) << exported.save.warning;

    const std::string written = read_text_file(exported.written_path);
    EXPECT_NE(written.find("over \"plate\""), std::string::npos) << written;
    EXPECT_NE(written.find("rel material:binding = </World/grey>"), std::string::npos) << written;
    EXPECT_NE(written.find("MaterialBindingAPI"), std::string::npos) << written;
}

TEST(Binding_override_export, a_rebound_group_of_facets_writes_the_binding_on_the_group)
{
    const Binding_override_scene scene{2};
    const Reference_export       exported{scene.root, binding_save_references(scene.carrier), "written_binding_subset_override.usda"};
    EXPECT_TRUE(exported.save.error.empty()) << exported.save.error;
    EXPECT_TRUE(exported.save.warning.empty()) << exported.save.warning;

    const std::string written = read_text_file(exported.written_path);
    EXPECT_NE(written.find("over \"back\""), std::string::npos) << written;
    EXPECT_NE(written.find("rel material:binding = </World/grey>"), std::string::npos) << written;
}

TEST(Binding_override_export, a_written_binding_reads_back_the_same)
{
    const Binding_override_scene scene{2};
    const Reference_export       exported{scene.root, binding_save_references(scene.carrier), "written_binding_reload.usda"};
    ASSERT_TRUE(exported.reloaded.error.empty()) << exported.reloaded.error;
    ASSERT_EQ(exported.reloaded.data.references.size(), 1u);
    const erhe::scene::Instance_override* back = find_override(exported.reloaded.data.references[0].overrides, "plate/back");
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->material_path, "/World/grey");
}

// The arc's target is the mesh itself, which erhe keeps as the carrier's one
// child and USD composes into the carrier prim (X1): a binding of the whole
// mesh is then the carrier's own, and a binding of one group of facets is an
// `over` below the carrier - the shape the Vehicles body assets author.
class Root_binding_scene final
{
public:
    explicit Root_binding_scene(const std::size_t primitive_count)
    {
        root    = std::make_shared<erhe::scene::Xform>("export_root");
        carrier = make_prim("geo");
        carrier->set_parent(root);

        grey = std::make_shared<erhe::primitive::Material>("grey");
        grey->enable_flag_bits(erhe::Item_flags::show_in_ui);
        grey->set_parent(root);

        template_plate = std::make_shared<erhe::scene::Mesh>("plate");
        clone_plate    = std::make_shared<erhe::scene::Mesh>("plate");
        for (std::size_t index = 0; index < primitive_count; ++index) {
            const std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>(
                (primitive_count == 1) ? "plate" : ((index == 0) ? "plate.front" : "plate.back")
            );
            template_plate->add_primitive(std::make_shared<erhe::primitive::Primitive>(geometry));
            clone_plate->add_primitive(std::make_shared<erhe::primitive::Primitive>(geometry));
        }
        clone_plate->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        clone_plate->set_reference(template_plate);
        clone_plate->set_parent(carrier);
        clone_plate->set_primitive_material(primitive_count - 1, grey);
    }

    std::shared_ptr<erhe::scene::Node>         root;
    std::shared_ptr<erhe::scene::Node>         carrier;
    std::shared_ptr<erhe::scene::Mesh>         template_plate;
    std::shared_ptr<erhe::scene::Mesh>         clone_plate;
    std::shared_ptr<erhe::primitive::Material> grey;
};

TEST(Binding_override_export, an_arc_target_that_rebinds_its_mesh_binds_on_the_carrier)
{
    const Root_binding_scene scene{1};
    const Reference_export   exported{scene.root, binding_save_references(scene.carrier), "written_root_binding.usda"};
    EXPECT_TRUE(exported.save.error.empty()) << exported.save.error;
    EXPECT_TRUE(exported.save.warning.empty()) << exported.save.warning;

    const std::string written = read_text_file(exported.written_path);
    EXPECT_NE(written.find("rel material:binding = </World/grey>"), std::string::npos) << written;
    EXPECT_NE(written.find("MaterialBindingAPI"), std::string::npos) << written;
    EXPECT_EQ(written.find("over \""), std::string::npos) << written;
}

TEST(Binding_override_export, an_arc_target_that_rebinds_one_group_writes_an_over_for_it)
{
    const Root_binding_scene scene{2};
    const Reference_export   exported{scene.root, binding_save_references(scene.carrier), "written_root_subset_binding.usda"};
    EXPECT_TRUE(exported.save.error.empty()) << exported.save.error;
    EXPECT_TRUE(exported.save.warning.empty()) << exported.save.warning;

    const std::string written = read_text_file(exported.written_path);
    EXPECT_NE(written.find("over \"back\""), std::string::npos) << written;
    EXPECT_NE(written.find("rel material:binding = </World/grey>"), std::string::npos) << written;

    ASSERT_TRUE(exported.reloaded.error.empty()) << exported.reloaded.error;
    ASSERT_EQ(exported.reloaded.data.references.size(), 1u);
    const erhe::scene::Instance_override* back = find_override(exported.reloaded.data.references[0].overrides, "back");
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->material_path, "/World/grey");
}

TEST(Binding_override_export, a_second_binding_save_is_byte_identical)
{
    const Binding_override_scene scene{2};
    const std::filesystem::path  first  = reference_temporary_directory() / "binding_double_save_1.usda";
    const std::filesystem::path  second = reference_temporary_directory() / "binding_double_save_2.usda";
    for (const std::filesystem::path& path : {first, second}) {
        const erhe::usd::Usd_save_arguments save_arguments{
            .path       = path,
            .root_node  = scene.root,
            .references = binding_save_references(scene.carrier)
        };
        const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;
    }
    EXPECT_EQ(read_text_file(first), read_text_file(second));
}

TEST(Override_export, a_second_save_is_byte_identical)
{
    const Override_scene        scene;
    const std::filesystem::path first  = reference_temporary_directory() / "override_double_save_1.usda";
    const std::filesystem::path second = reference_temporary_directory() / "override_double_save_2.usda";
    for (const std::filesystem::path& path : {first, second}) {
        const erhe::usd::Usd_save_arguments save_arguments{
            .path       = path,
            .root_node  = scene.root,
            .references = override_save_references(scene.carrier)
        };
        const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;
    }
    EXPECT_EQ(read_text_file(first), read_text_file(second));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Carriers that carry no transform of their own (doc/usd-compatibility-plan.md
// S1): a typeless `def` and a `Scope` that author arcs import as `Xform`
// carriers, which is what holds the prefab instances the arcs become.
// ---------------------------------------------------------------------------

[[nodiscard]] auto find_typed_prim(const erhe::usd::Usd_data& data, const std::string& name) -> std::shared_ptr<erhe::Typed>
{
    for (const std::shared_ptr<erhe::Typed>& prim : data.prims) {
        if (prim && (prim->get_name() == name)) {
            return prim;
        }
    }
    return {};
}

class Typeless_carrier_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments arguments{
            .path          = reference_test_data_path("references_typeless.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(arguments);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Typeless_carrier_import, a_typeless_carrier_and_a_scope_carrier_are_xform_carriers)
{
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.references.size(), 2u);

    for (const char* name : {"Carrier", "ScopeCarrier"}) {
        const std::shared_ptr<erhe::scene::Node> carrier = find_node(result.data, name);
        ASSERT_TRUE(carrier.operator bool()) << name;
        EXPECT_EQ(carrier->get_prim_type_name(), "Xform") << name;
        EXPECT_FALSE(find_typed_prim(result.data, name).operator bool()) << name;
    }

    EXPECT_EQ(result.data.references[0].stage_path, "/World/Carrier");
    EXPECT_EQ(result.data.references[0].item, find_node(result.data, "Carrier"));
    ASSERT_EQ(result.data.references[0].references.size(), 1u);
    EXPECT_EQ(result.data.references[0].references[0].asset_path, "reftarget.usda");
    EXPECT_EQ(result.data.references[0].references[0].prim_path, "/Library/Gadget");

    EXPECT_EQ(result.data.references[1].stage_path, "/World/ScopeCarrier");
    EXPECT_EQ(result.data.references[1].item, find_node(result.data, "ScopeCarrier"));
}

TEST_F(Typeless_carrier_import, a_scope_without_arcs_stays_a_scope)
{
    ASSERT_TRUE(result.error.empty()) << result.error;
    const std::shared_ptr<erhe::Typed> scope = find_typed_prim(result.data, "PlainScope");
    ASSERT_TRUE(scope.operator bool());
    EXPECT_EQ(scope->get_prim_type_name(), "Scope");
    EXPECT_FALSE(find_node(result.data, "PlainScope").operator bool());
}

// The writer spells an arc carrier `def Xform`, which is the same composition
// spelled more explicitly, and that spelling is the round trip's fixed point
// from the first save on.
TEST(Typeless_carrier_export, a_carrier_is_written_as_an_xform_and_the_save_is_a_fixed_point)
{
    const std::filesystem::path directory = reference_temporary_directory();
    for (const char* file_name : {"references_typeless.usda", "reftarget.usda"}) {
        std::error_code error_code{};
        std::filesystem::copy_file(
            reference_test_data_path(file_name),
            directory / file_name,
            std::filesystem::copy_options::overwrite_existing,
            error_code
        );
        ASSERT_FALSE(static_cast<bool>(error_code)) << file_name << ": " << error_code.message();
    }
    const std::filesystem::path stage_path = directory / "references_typeless.usda";

    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments load_arguments{
        .path          = stage_path,
        .root_node     = root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result loaded = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    ASSERT_EQ(loaded.data.references.size(), 2u);

    std::vector<erhe::usd::Usd_save_prim_references> save_references;
    for (const erhe::usd::Usd_prim_references& entry : loaded.data.references) {
        erhe::usd::Usd_save_prim_references save_entry{};
        save_entry.item = entry.item;
        for (const erhe::usd::Usd_reference& reference : entry.references) {
            save_entry.references.push_back(
                erhe::usd::Usd_save_reference{
                    .source_path = reference.asset_path.empty() ? stage_path : (directory / reference.asset_path),
                    .prim_path   = reference.prim_path,
                    .kind        = reference.kind
                }
            );
        }
        save_references.push_back(std::move(save_entry));
    }

    const erhe::usd::Usd_save_arguments save_arguments{
        .path       = stage_path,
        .root_node  = root,
        .references = save_references
    };
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;

    const std::string written = read_text_file(stage_path);
    EXPECT_NE(written.find("def Xform \"Carrier\""), std::string::npos) << written;
    EXPECT_NE(written.find("def Xform \"ScopeCarrier\""), std::string::npos) << written;
    EXPECT_EQ(written.find("def \"Carrier\""), std::string::npos) << written;
    EXPECT_NE(written.find("def Scope \"PlainScope\""), std::string::npos) << written;

    // Reading the written file back and writing it again changes nothing.
    const std::shared_ptr<erhe::scene::Node> reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_arguments reload_arguments{
        .path          = stage_path,
        .root_node     = reloaded_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(reload_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    ASSERT_EQ(reloaded.data.references.size(), 2u);

    std::vector<erhe::usd::Usd_save_prim_references> resave_references;
    for (const erhe::usd::Usd_prim_references& entry : reloaded.data.references) {
        erhe::usd::Usd_save_prim_references save_entry{};
        save_entry.item = entry.item;
        for (const erhe::usd::Usd_reference& reference : entry.references) {
            save_entry.references.push_back(
                erhe::usd::Usd_save_reference{
                    .source_path = reference.asset_path.empty() ? stage_path : (directory / reference.asset_path),
                    .prim_path   = reference.prim_path,
                    .kind        = reference.kind
                }
            );
        }
        resave_references.push_back(std::move(save_entry));
    }
    const erhe::usd::Usd_save_arguments resave_arguments{
        .path       = stage_path,
        .root_node  = reloaded_root,
        .references = resave_references
    };
    const erhe::usd::Usd_save_result resave = erhe::usd::save_usda(resave_arguments);
    ASSERT_TRUE(resave.error.empty()) << resave.error;
    EXPECT_EQ(read_text_file(stage_path), written);
}

// ---------------------------------------------------------------------------
// Internal references whose target is itself a referencing prim
// (doc/usd-compatibility-plan.md X1): the shape the usd-wg
// `OverridingReferencedInternalReferencesTest` asset has - a component file
// keeps its subcomponents as prims of the same layer and reaches them through
// arcs with an empty asset path, and the referencing layer authors an `over`
// over one of them.
// ---------------------------------------------------------------------------

class Internal_nested_reference_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments arguments{
            .path          = reference_test_data_path("references_internal_nested.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(arguments);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Internal_nested_reference_import, both_arcs_are_reported_in_authored_order)
{
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.references.size(), 2u);

    const erhe::usd::Usd_prim_references& inner = result.data.references[0];
    EXPECT_EQ(inner.stage_path, "/World/Prefabs/assembly");
    ASSERT_EQ(inner.references.size(), 1u);
    EXPECT_TRUE(inner.references[0].asset_path.empty());
    EXPECT_EQ(inner.references[0].prim_path, "/World/Prefabs/bolt");

    const erhe::usd::Usd_prim_references& outer = result.data.references[1];
    EXPECT_EQ(outer.stage_path, "/World/Geometry/assembly_01");
    ASSERT_EQ(outer.references.size(), 1u);
    EXPECT_TRUE(outer.references[0].asset_path.empty());
    EXPECT_EQ(outer.references[0].prim_path, "/World/Prefabs/assembly");
}

TEST_F(Internal_nested_reference_import, the_arc_targets_are_imported_prims_and_the_carriers_are_empty)
{
    ASSERT_TRUE(result.error.empty()) << result.error;

    // Both targets are authored in this layer, so both import as the ordinary
    // prims they are - a template load reaches them by their prim path.
    for (const char* name : {"bolt", "bolt_mesh", "assembly"}) {
        EXPECT_TRUE(find_node(result.data, name).operator bool()) << name;
    }

    // The content each arc names is supplied by the instantiation, so neither
    // carrier holds it at import.
    for (const char* name : {"assembly", "assembly_01"}) {
        const std::shared_ptr<erhe::scene::Node> carrier = find_node(result.data, name);
        ASSERT_TRUE(carrier.operator bool()) << name;
        EXPECT_TRUE(carrier->get_children().empty()) << name;
    }
}

TEST_F(Internal_nested_reference_import, a_carrier_that_is_itself_a_target_keeps_its_own_transform)
{
    ASSERT_TRUE(result.error.empty()) << result.error;
    const std::shared_ptr<erhe::scene::Node> assembly = find_node(result.data, "assembly");
    ASSERT_TRUE(assembly.operator bool());
    const glm::vec3 translation = glm::vec3{assembly->parent_from_node_transform().get_matrix()[3]};
    EXPECT_FLOAT_EQ(translation.x, 0.0f);
    EXPECT_FLOAT_EQ(translation.y, 1.0f);
    EXPECT_FLOAT_EQ(translation.z, 0.0f);
}

TEST_F(Internal_nested_reference_import, the_over_below_the_outer_carrier_is_read_as_an_override)
{
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.data.references.size(), 2u);
    const erhe::scene::Instance_override* mesh_override = find_override(result.data.references[1].overrides, "bolt_mesh");
    ASSERT_NE(mesh_override, nullptr);
    const std::string* visible = find_override_value(*mesh_override, "visible");
    ASSERT_NE(visible, nullptr);
    EXPECT_EQ(*visible, "false");
}
