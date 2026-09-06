#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
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

// A prim instantiation put under a carrier: sealed, the way
// attach_prefab_instance seals a clone.
[[nodiscard]] auto make_instance_content_prim(const char* name) -> std::shared_ptr<erhe::scene::Xform>
{
    std::shared_ptr<erhe::scene::Xform> node = make_prim(name);
    node->enable_flag_bits(erhe::Item_flags::lock_edit);
    return node;
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
    make_instance_content_prim("instance_content")->set_parent(carrier);

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
    make_instance_content_prim("instance_content")->set_parent(carrier);
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

} // anonymous namespace
