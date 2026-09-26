// A constant `primvars:displayColor` (USD's one color for a whole surface):
// the importer makes it the mesh's `Gprim.display_color` local value beside
// the vertex colors it bakes, and the writer authors it back as the constant
// primvar - once, in its native form, never as an `erhe:Gprim:display_color`
// custom attribute.

#include "test_temporary_directory.hpp"

#include "erhe_scene/gprim.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = erhe_usd_test::process_temporary_directory() / "erhe_usd_display_color_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto find_mesh(const erhe::usd::Usd_data& data, const std::string& name) -> std::shared_ptr<erhe::scene::Mesh>
{
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : data.meshes) {
        if (mesh && (mesh->get_name() == name)) {
            return mesh;
        }
    }
    return {};
}

[[nodiscard]] auto read_text(const std::filesystem::path& path) -> std::string
{
    std::ifstream     stream{path, std::ios::binary};
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] auto count_occurrences(const std::string& text, const std::string& needle) -> std::size_t
{
    std::size_t count    = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

class Display_color_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("display_color_mesh.usda"),
            .root_node     = source_root,
            .mesh_layer_id = 0
        };
        source = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("display_color.usda");
        const erhe::usd::Usd_save_arguments save_arguments{
            .path      = written_path,
            .root_node = source_root,
            .materials = source.data.materials
        };
        save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        const erhe::usd::Usd_load_arguments reload_arguments{
            .path          = written_path,
            .root_node     = reloaded_root,
            .mesh_layer_id = 0
        };
        reloaded = erhe::usd::load_usd(reload_arguments);
        ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

        rewritten_path = temporary_path("display_color_again.usda");
        const erhe::usd::Usd_save_arguments rewrite_arguments{
            .path      = rewritten_path,
            .root_node = reloaded_root,
            .materials = reloaded.data.materials
        };
        rewrite = erhe::usd::save_usda(rewrite_arguments);
        ASSERT_TRUE(rewrite.error.empty()) << rewrite.error;
    }

    std::shared_ptr<erhe::scene::Node> source_root;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    std::filesystem::path              written_path;
    std::filesystem::path              rewritten_path;
    erhe::usd::Usd_load_result         source;
    erhe::usd::Usd_save_result         save;
    erhe::usd::Usd_load_result         reloaded;
    erhe::usd::Usd_save_result         rewrite;
};

} // namespace

TEST_F(Display_color_round_trip, a_constant_display_color_is_a_local_value)
{
    // The geometry-normative build and the soup build agree: the value is the
    // prim's, not the build's.
    for (const char* name : {"Frame", "FrameSoup"}) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = find_mesh(source.data, name);
        ASSERT_TRUE(mesh.operator bool()) << name;
        const std::optional<glm::vec3> color = mesh->read_local_value(erhe::scene::Gprim::display_color_property);
        ASSERT_TRUE(color.has_value()) << name;
        EXPECT_NEAR(color.value().x, 0.7f, 1e-6f) << name;
        EXPECT_NEAR(color.value().y, 0.0f, 1e-6f) << name;
        EXPECT_NEAR(color.value().z, 0.7f, 1e-6f) << name;
    }

    for (const char* name : {"Plain", "PlainSoup"}) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = find_mesh(source.data, name);
        ASSERT_TRUE(mesh.operator bool()) << name;
        EXPECT_FALSE(mesh->read_local_value(erhe::scene::Gprim::display_color_property).has_value()) << name;
        EXPECT_EQ(mesh->get_display_color(), erhe::scene::Gprim::default_display_color) << name;
    }
}

TEST_F(Display_color_round_trip, the_constant_is_written_back_in_its_native_form)
{
    const std::string written = read_text(written_path);
    // One `primvars:displayColor` per colored mesh, and no faceVarying array
    // repeating the same value per corner.
    EXPECT_EQ(count_occurrences(written, "primvars:displayColor"), std::size_t{2});
    EXPECT_EQ(count_occurrences(written, "interpolation = \"constant\""), std::size_t{2});
    EXPECT_EQ(written.find("primvars:displayOpacity"), std::string::npos);
    EXPECT_EQ(written.find("erhe:Gprim:display_color"), std::string::npos);
}

TEST_F(Display_color_round_trip, the_value_survives_the_round_trip)
{
    const std::shared_ptr<erhe::scene::Mesh> frame = find_mesh(reloaded.data, "Frame");
    ASSERT_TRUE(frame.operator bool());
    const std::optional<glm::vec3> color = frame->read_local_value(erhe::scene::Gprim::display_color_property);
    ASSERT_TRUE(color.has_value());
    EXPECT_NEAR(color.value().x, 0.7f, 1e-6f);
    EXPECT_NEAR(color.value().y, 0.0f, 1e-6f);
    EXPECT_NEAR(color.value().z, 0.7f, 1e-6f);

    const std::shared_ptr<erhe::scene::Mesh> plain = find_mesh(reloaded.data, "Plain");
    ASSERT_TRUE(plain.operator bool());
    EXPECT_FALSE(plain->read_local_value(erhe::scene::Gprim::display_color_property).has_value());
}

TEST_F(Display_color_round_trip, the_second_save_is_byte_identical)
{
    EXPECT_EQ(read_text(rewritten_path), read_text(written_path));
}
