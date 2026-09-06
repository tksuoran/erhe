#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene/xform_op.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
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
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_xform_op_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_lines(const std::filesystem::path& path) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::ifstream            stream{path};
    std::string              line;
    while (std::getline(stream, line)) {
        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

// The `xformOp` attribute lines and the `xformOpOrder` line of one prim, with
// their leading indentation removed so the same prim compares equal wherever
// it sits. The fixture prims are leaves, so the block ends at the first line
// that closes a brace.
[[nodiscard]] auto xform_op_lines(const std::filesystem::path& path, const std::string& prim_name) -> std::vector<std::string>
{
    const std::vector<std::string> lines = read_lines(path);
    const std::string              header = "\"" + prim_name + "\"";
    std::vector<std::string>       result;
    bool                           in_prim = false;
    for (const std::string& line : lines) {
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        const std::string trimmed = line.substr(first);
        if (!in_prim) {
            if ((trimmed.rfind("def ", 0) == 0) && (trimmed.find(header) != std::string::npos)) {
                in_prim = true;
            }
            continue;
        }
        if (trimmed == "}") {
            break;
        }
        if (trimmed.find("xformOp") != std::string::npos) {
            result.push_back(trimmed);
        }
    }
    return result;
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

// Load `xform_ops.usda` and write it back out. The fixture is in the writer's
// own output spelling, so every line the writer produces for a prim that
// authored ops has to come back byte for byte (doc/usd-compatibility-plan.md
// M8).
class Xform_op_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_path = test_data_path("xform_ops.usda");
        root        = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = source_path,
            .root_node     = root,
            .mesh_layer_id = 0
        };
        loaded = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    }

    void save(const char* file_name)
    {
        written_path = temporary_path(file_name);
        const erhe::usd::Usd_save_arguments save_arguments{
            .path      = written_path,
            .root_node = root
        };
        const erhe::usd::Usd_save_result save_result = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save_result.error.empty()) << save_result.error;
    }

    std::filesystem::path              source_path;
    std::filesystem::path              written_path;
    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         loaded;
};

TEST_F(Xform_op_round_trip, three_op_stack_is_written_back_byte_for_byte)
{
    save("three_ops.usda");
    const std::vector<std::string> expected = xform_op_lines(source_path,  "three_ops");
    ASSERT_EQ(expected.size(), 4u); // three ops plus xformOpOrder
    EXPECT_EQ(xform_op_lines(written_path, "three_ops"), expected);

    // The fixture is the writer's own output spelling, so nothing else moved
    // either: the whole file comes back line for line.
    EXPECT_EQ(read_lines(written_path), read_lines(source_path));
}

// The written file is itself a valid source for the same stack: a second load
// and save reproduces it, so the round trip is a fixed point rather than a
// one-off match.
TEST_F(Xform_op_round_trip, a_second_round_trip_is_identical)
{
    save("three_ops.usda");
    const std::vector<std::string> first = xform_op_lines(written_path, "three_ops");

    const std::shared_ptr<erhe::scene::Node> second_root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments      load_arguments{
        .path          = written_path,
        .root_node     = second_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

    const std::filesystem::path         second_path = temporary_path("three_ops_again.usda");
    const erhe::usd::Usd_save_arguments save_arguments{
        .path      = second_path,
        .root_node = second_root
    };
    const erhe::usd::Usd_save_result save_result = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save_result.error.empty()) << save_result.error;

    EXPECT_EQ(xform_op_lines(second_path, "three_ops"), first);
}

TEST_F(Xform_op_round_trip, pivot_suffixes_inversion_and_reset_survive)
{
    save("pivot.usda");
    const std::vector<std::string> expected = xform_op_lines(source_path, "pivot");
    ASSERT_EQ(expected.size(), 3u); // two op values plus xformOpOrder
    EXPECT_EQ(xform_op_lines(written_path, "pivot"), expected);

    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "pivot");
    ASSERT_TRUE(node.operator bool());
    const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
    ASSERT_TRUE(stack != nullptr);
    EXPECT_TRUE(stack->reset_xform_stack);
    ASSERT_EQ(stack->ops.size(), 3u);
    EXPECT_EQ(stack->ops[0].suffix, "pivot");
    EXPECT_FALSE(stack->ops[0].inverted);
    EXPECT_EQ(stack->ops[2].suffix, "pivot");
    EXPECT_TRUE(stack->ops[2].inverted);
}

TEST_F(Xform_op_round_trip, matrix_op_survives)
{
    save("matrix_op.usda");
    const std::vector<std::string> expected = xform_op_lines(source_path, "matrix_op");
    ASSERT_EQ(expected.size(), 2u);
    EXPECT_EQ(xform_op_lines(written_path, "matrix_op"), expected);
}

TEST_F(Xform_op_round_trip, a_prim_without_ops_writes_no_ops)
{
    save("no_ops.usda");
    EXPECT_TRUE(xform_op_lines(source_path,  "no_ops").empty());
    EXPECT_TRUE(xform_op_lines(written_path, "no_ops").empty());

    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "no_ops");
    ASSERT_TRUE(node.operator bool());
    const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
    ASSERT_TRUE(stack != nullptr);
    EXPECT_TRUE(stack->ops.empty());
}

// Every imported prim carries a stack that composes to the transform the
// stage evaluates - the importer drops a stack that does not, so this is what
// says the two agree on the fixtures.
TEST_F(Xform_op_round_trip, every_imported_prim_carries_its_stack)
{
    ASSERT_FALSE(loaded.data.nodes.empty());
    for (const std::shared_ptr<erhe::scene::Node>& node : loaded.data.nodes) {
        ASSERT_TRUE(node.operator bool());
        const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
        ASSERT_TRUE(stack != nullptr) << node->get_name();
        const glm::mat4 composed{stack->compose()};
        const glm::mat4 local = node->parent_from_node_transform().get_matrix();
        for (int j = 0; j < 4; ++j) {
            for (int i = 0; i < 4; ++i) {
                EXPECT_NEAR(composed[j][i], local[j][i], 1e-5f) << node->get_name() << " [" << j << "][" << i << "]";
            }
        }
    }
}

// A moved prim's edit lands in the op the stack designates: the translate op
// carries the new value, and every other op is written back untouched.
TEST_F(Xform_op_round_trip, a_move_lands_in_the_translate_op)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "three_ops");
    ASSERT_TRUE(node.operator bool());

    glm::mat4 matrix = node->parent_from_node_transform().get_matrix();
    matrix[3] = glm::vec4{4.0f, 5.0f, 6.0f, 1.0f};
    node->set_parent_from_node(matrix);

    save("moved.usda");
    const std::vector<std::string> before = xform_op_lines(source_path,  "three_ops");
    const std::vector<std::string> after  = xform_op_lines(written_path, "three_ops");
    ASSERT_EQ(after.size(), before.size());
    EXPECT_EQ(after[0], "double3 xformOp:translate = (4, 5, 6)");
    EXPECT_NE(after[0], before[0]);
    EXPECT_EQ(after[1], before[1]); // rotateXYZ
    EXPECT_EQ(after[2], before[2]); // scale
    EXPECT_EQ(after[3], before[3]); // xformOpOrder
}

} // anonymous namespace
