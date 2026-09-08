#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_scene/point_instancer.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] auto instancer_test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto instancer_temporary_directory() -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_point_instancer_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory;
}

[[nodiscard]] auto read_text_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream     stream{path};
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
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

// The arc entry recorded for the prim at `stage_path`, null when the load
// recorded none.
[[nodiscard]] auto find_reference_entry(
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

// The record the editor hands the writer, built the way the editor builds it:
// the instance prims in tree order, each with the prototype it references -
// which the load already resolved, and which the editor re-derives from the
// instance's arc target.
[[nodiscard]] auto make_save_record(const erhe::usd::Usd_load_result& loaded) -> erhe::usd::Usd_save_point_instancer
{
    const erhe::usd::Usd_point_instancer& record = loaded.data.point_instancers.at(0);
    erhe::usd::Usd_save_point_instancer save_instancer{};
    save_instancer.item = record.item;
    for (std::size_t instance = 0, end = record.instance_items.size(); instance < end; ++instance) {
        save_instancer.instances.push_back(
            erhe::usd::Usd_save_point_instance{
                .item        = record.instance_items[instance],
                .proto_index = record.instances[instance].proto_index
            }
        );
    }
    return save_instancer;
}

class Point_instancer_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments arguments{
            .path          = instancer_test_data_path("point_instancer.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(arguments);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Point_instancer_import, load_succeeds)
{
    EXPECT_TRUE(result.error.empty()) << result.error;
}

TEST_F(Point_instancer_import, the_prim_is_a_point_instancer)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, "Scatter");
    ASSERT_TRUE(node);
    const std::shared_ptr<erhe::scene::Point_instancer> instancer =
        std::dynamic_pointer_cast<erhe::scene::Point_instancer>(node);
    ASSERT_TRUE(instancer);
    EXPECT_EQ(instancer->get_class_type_name(), "PointInstancer");
}

// The class carries no `proto_indices` array: `protoIndices` is the record's,
// and a save takes it from the instance prims.
TEST_F(Point_instancer_import, the_prim_carries_no_proto_indices_array)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, "Scatter");
    ASSERT_TRUE(node);
    std::vector<const erhe::property::Dependency_property*> local_properties;
    node->for_each_local_value(
        [&local_properties](const erhe::property::Dependency_property& property, const erhe::property::Property_value&) {
            local_properties.push_back(&property);
        }
    );
    for (const erhe::property::Dependency_property* property : local_properties) {
        EXPECT_NE(property->get_name(), "proto_indices");
    }
}

TEST_F(Point_instancer_import, the_record_names_the_prototypes_and_the_instances)
{
    ASSERT_EQ(result.data.point_instancers.size(), 1u);
    const erhe::usd::Usd_point_instancer& record = result.data.point_instancers[0];
    EXPECT_EQ(record.stage_path, "/World/Scatter");
    ASSERT_EQ(record.prototype_paths.size(), 2u);
    EXPECT_EQ(record.prototype_paths[0], "/World/Scatter/Prototypes/BoxProto");
    EXPECT_EQ(record.prototype_paths[1], "/World/Scatter/Prototypes/BallProto");
    ASSERT_EQ(record.instances.size(), 4u);
    ASSERT_EQ(record.instance_items.size(), 4u);

    const glm::vec3 expected_positions[4] = {
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{2.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 2.0f},
        glm::vec3{2.0f, 0.0f, 2.0f}
    };
    const float       expected_scales     [4] = {1.0f, 2.0f, 0.5f, 1.0f};
    const std::size_t expected_proto_index[4] = {0u, 1u, 0u, 1u};
    for (std::size_t instance = 0; instance < 4u; ++instance) {
        const erhe::usd::Usd_point_instance& entry = record.instances[instance];
        EXPECT_EQ(entry.proto_index, expected_proto_index[instance]) << instance;
        EXPECT_NEAR(entry.transform[3][0], expected_positions[instance].x, 1e-5f) << instance;
        EXPECT_NEAR(entry.transform[3][1], expected_positions[instance].y, 1e-5f) << instance;
        EXPECT_NEAR(entry.transform[3][2], expected_positions[instance].z, 1e-5f) << instance;
        EXPECT_NEAR(entry.transform[0][0], expected_scales[instance], 1e-5f) << instance;
        EXPECT_NEAR(entry.transform[1][1], expected_scales[instance], 1e-5f) << instance;
        EXPECT_NEAR(entry.transform[2][2], expected_scales[instance], 1e-5f) << instance;
    }
}

TEST_F(Point_instancer_import, every_instance_is_a_child_prim_carrying_an_internal_reference)
{
    const std::shared_ptr<erhe::scene::Node> instancer = find_node(result.data, "Scatter");
    ASSERT_TRUE(instancer);

    const char* instance_names  [4] = {"BoxProto_0", "BallProto_1", "BoxProto_2", "BallProto_3"};
    const char* prototype_paths[4] = {
        "/World/Scatter/Prototypes/BoxProto",
        "/World/Scatter/Prototypes/BallProto",
        "/World/Scatter/Prototypes/BoxProto",
        "/World/Scatter/Prototypes/BallProto"
    };
    for (std::size_t instance = 0; instance < 4u; ++instance) {
        const std::shared_ptr<erhe::scene::Node> instance_node = find_node(result.data, instance_names[instance]);
        ASSERT_TRUE(instance_node) << instance_names[instance];
        EXPECT_EQ(instance_node->get_parent().lock().get(), instancer.get()) << instance_names[instance];

        const erhe::usd::Usd_prim_references* entry = find_reference_entry(
            result.data,
            std::string{"/World/Scatter/"} + instance_names[instance]
        );
        ASSERT_TRUE(entry != nullptr) << instance_names[instance];
        ASSERT_EQ(entry->references.size(), 1u) << instance_names[instance];
        EXPECT_TRUE(entry->references[0].asset_path.empty()) << instance_names[instance];
        EXPECT_EQ(entry->references[0].prim_path, prototype_paths[instance]);
        EXPECT_EQ(entry->references[0].kind, erhe::usd::Usd_reference_kind::reference);
    }
}

TEST_F(Point_instancer_import, the_prototype_subtree_is_held_abstract)
{
    for (const char* name : {"BoxProto", "BallProto", "box", "ball"}) {
        const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, name);
        ASSERT_TRUE(node) << name;
        EXPECT_EQ(node->get_flag_bits() & erhe::Item_flags::content, 0u) << name;
    }
    // The instances are content: they are what the scene draws.
    for (const char* name : {"BoxProto_0", "BallProto_1"}) {
        const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, name);
        ASSERT_TRUE(node) << name;
        EXPECT_NE(node->get_flag_bits() & erhe::Item_flags::content, 0u) << name;
    }
}

// Write the loaded scene back and read the result. The instances are handed
// to the writer the way the editor hands them over: the instancer's children
// that the load made instances of.
class Point_instancer_export : public Point_instancer_import
{
protected:
    void SetUp() override
    {
        Point_instancer_import::SetUp();
        ASSERT_TRUE(result.error.empty()) << result.error;
        ASSERT_EQ(result.data.point_instancers.size(), 1u);

        const erhe::usd::Usd_save_point_instancer save_instancer = make_save_record(result);

        written_path = instancer_temporary_directory() / "point_instancer_written.usda";
        const erhe::usd::Usd_save_arguments save_arguments{
            .path             = written_path,
            .root_node        = root,
            .point_instancers = {save_instancer}
        };
        save          = erhe::usd::save_usda(save_arguments);
        written_text  = read_text_file(written_path);

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = written_path,
            .root_node     = reloaded_root,
            .mesh_layer_id = 0
        };
        reloaded = erhe::usd::load_usd(load_arguments);
    }

    std::filesystem::path              written_path;
    std::string                        written_text;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    erhe::usd::Usd_save_result         save;
    erhe::usd::Usd_load_result         reloaded;
};

TEST_F(Point_instancer_export, the_arrays_are_written_back)
{
    ASSERT_TRUE(save.error.empty()) << save.error;
    EXPECT_NE(written_text.find("def PointInstancer \"Scatter\""), std::string::npos) << written_text;
    EXPECT_NE(written_text.find("int[] protoIndices = [0, 1, 0, 1]"), std::string::npos) << written_text;
    EXPECT_NE(written_text.find("point3f[] positions"), std::string::npos) << written_text;
    EXPECT_NE(written_text.find("float3[] scales"), std::string::npos) << written_text;
    EXPECT_NE(written_text.find("rel prototypes"), std::string::npos) << written_text;
}

TEST_F(Point_instancer_export, the_instance_prims_are_not_written)
{
    ASSERT_TRUE(save.error.empty()) << save.error;
    for (const char* name : {"BoxProto_0", "BallProto_1", "BoxProto_2", "BallProto_3"}) {
        EXPECT_EQ(written_text.find(name), std::string::npos) << name;
    }
    // The prototypes are: they are the prims the instances reference.
    EXPECT_NE(written_text.find("def Xform \"BoxProto\""), std::string::npos) << written_text;
    EXPECT_NE(written_text.find("def Xform \"BallProto\""), std::string::npos) << written_text;
}

TEST_F(Point_instancer_export, the_written_file_reloads_to_the_same_instances)
{
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    ASSERT_EQ(reloaded.data.point_instancers.size(), 1u);
    const erhe::usd::Usd_point_instancer& before = result.data.point_instancers[0];
    const erhe::usd::Usd_point_instancer& after  = reloaded.data.point_instancers[0];
    ASSERT_EQ(after.instances.size(), before.instances.size());
    for (std::size_t instance = 0, end = before.instances.size(); instance < end; ++instance) {
        EXPECT_EQ(after.instances[instance].proto_index, before.instances[instance].proto_index) << instance;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                EXPECT_NEAR(
                    after.instances[instance].transform[column][row],
                    before.instances[instance].transform[column][row],
                    1e-4f
                ) << instance << " " << column << " " << row;
            }
        }
    }
}

TEST_F(Point_instancer_export, a_moved_instance_persists)
{
    ASSERT_TRUE(save.error.empty()) << save.error;

    const std::shared_ptr<erhe::scene::Node> instance = find_node(result.data, "BallProto_1");
    ASSERT_TRUE(instance);
    instance->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{7.0f, 8.0f, 9.0f}));

    const erhe::usd::Usd_save_point_instancer save_instancer = make_save_record(result);
    const std::filesystem::path moved_path = instancer_temporary_directory() / "point_instancer_moved.usda";
    const erhe::usd::Usd_save_arguments save_arguments{
        .path             = moved_path,
        .root_node        = root,
        .point_instancers = {save_instancer}
    };
    const erhe::usd::Usd_save_result moved_save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(moved_save.error.empty()) << moved_save.error;

    const std::shared_ptr<erhe::scene::Node> moved_root = std::make_shared<erhe::scene::Xform>("moved_root");
    const erhe::usd::Usd_load_arguments load_arguments{
        .path          = moved_path,
        .root_node     = moved_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result moved = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(moved.error.empty()) << moved.error;
    ASSERT_EQ(moved.data.point_instancers.size(), 1u);
    ASSERT_EQ(moved.data.point_instancers[0].instances.size(), 4u);
    const glm::mat4& transform = moved.data.point_instancers[0].instances[1].transform;
    EXPECT_NEAR(transform[3][0], 7.0f, 1e-4f);
    EXPECT_NEAR(transform[3][1], 8.0f, 1e-4f);
    EXPECT_NEAR(transform[3][2], 9.0f, 1e-4f);
}

// Save one loaded file again, under `file_name`, and hand the writer the
// instancer the way the editor does.
[[nodiscard]] auto save_again(
    const erhe::usd::Usd_load_result&         loaded,
    const std::shared_ptr<erhe::scene::Node>& loaded_root,
    const char*                               file_name
) -> std::string
{
    const erhe::usd::Usd_save_point_instancer save_instancer = make_save_record(loaded);
    const std::filesystem::path path = instancer_temporary_directory() / file_name;
    const erhe::usd::Usd_save_arguments save_arguments{
        .path             = path,
        .root_node        = loaded_root,
        .point_instancers = {save_instancer}
    };
    const erhe::usd::Usd_save_result result = erhe::usd::save_usda(save_arguments);
    EXPECT_TRUE(result.error.empty()) << result.error;
    return read_text_file(path);
}

// The written file is a fixed point from the first reload on. The FIRST save
// is not compared: this fixture's prototypes are `Cube` and `Sphere` schema
// prims, whose tessellation is generated, and erhe's own mesh build settles
// the vertex order only once the written `Mesh` prims have been read back
// (the E4a brush trap, in the same shape).
TEST_F(Point_instancer_export, saving_the_reloaded_file_again_is_byte_identical)
{
    ASSERT_TRUE(save.error.empty()) << save.error;
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    ASSERT_EQ(reloaded.data.point_instancers.size(), 1u);

    const std::string second_text = save_again(reloaded, reloaded_root, "point_instancer_second.usda");

    const std::shared_ptr<erhe::scene::Node> third_root = std::make_shared<erhe::scene::Xform>("third_root");
    const erhe::usd::Usd_load_arguments load_arguments{
        .path          = instancer_temporary_directory() / "point_instancer_second.usda",
        .root_node     = third_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result third = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(third.error.empty()) << third.error;
    ASSERT_EQ(third.data.point_instancers.size(), 1u);

    EXPECT_EQ(save_again(third, third_root, "point_instancer_third.usda"), second_text);
}

} // anonymous namespace
