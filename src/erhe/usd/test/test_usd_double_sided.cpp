// `doubleSided` of a geometry prim (USD `UsdGeomGprim.doubleSided`): an
// authored opinion lands on the mesh's `Gprim.double_sided` property as a
// local value, an unauthored one leaves the property at its default (M4),
// and the writer authors exactly the local ones back.

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
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_double_sided_tests";
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

[[nodiscard]] auto count_double_sided_lines(const std::string& text) -> std::size_t
{
    std::size_t       count    = 0;
    std::size_t       position = 0;
    const std::string needle{"doubleSided"};
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

class Double_sided_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("double_sided.usda"),
            .root_node     = source_root,
            .mesh_layer_id = 0
        };
        source = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("double_sided.usda");
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

        rewritten_path = temporary_path("double_sided_again.usda");
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

TEST_F(Double_sided_round_trip, an_authored_opinion_is_a_local_value)
{
    const std::shared_ptr<erhe::scene::Mesh> card = find_mesh(source.data, "card");
    ASSERT_TRUE(card.operator bool());
    EXPECT_EQ(card->read_local_value(erhe::scene::Gprim::double_sided_property), std::optional<bool>{true});
    EXPECT_TRUE(card->get_double_sided());

    // `doubleSided = 0` says something even though it equals the default (D32).
    const std::shared_ptr<erhe::scene::Mesh> single = find_mesh(source.data, "single");
    ASSERT_TRUE(single.operator bool());
    EXPECT_EQ(single->read_local_value(erhe::scene::Gprim::double_sided_property), std::optional<bool>{false});
    EXPECT_FALSE(single->get_double_sided());
}

TEST_F(Double_sided_round_trip, a_prim_without_the_attribute_has_no_local_value)
{
    const std::shared_ptr<erhe::scene::Mesh> plate = find_mesh(source.data, "plate");
    ASSERT_TRUE(plate.operator bool());
    EXPECT_FALSE(plate->read_local_value(erhe::scene::Gprim::double_sided_property).has_value());
    EXPECT_FALSE(plate->get_double_sided());
}

TEST_F(Double_sided_round_trip, only_the_local_values_are_written_back)
{
    const std::string written = read_text(written_path);
    EXPECT_EQ(count_double_sided_lines(written), std::size_t{2});
    EXPECT_NE(written.find("uniform bool doubleSided = 1"), std::string::npos);
    EXPECT_NE(written.find("uniform bool doubleSided = 0"), std::string::npos);
    // The value travels in the schema attribute, never also as a custom one.
    EXPECT_EQ(written.find("erhe:Gprim:double_sided"), std::string::npos);
}

TEST_F(Double_sided_round_trip, the_values_survive_the_round_trip)
{
    const std::shared_ptr<erhe::scene::Mesh> card = find_mesh(reloaded.data, "card");
    ASSERT_TRUE(card.operator bool());
    EXPECT_EQ(card->read_local_value(erhe::scene::Gprim::double_sided_property), std::optional<bool>{true});

    const std::shared_ptr<erhe::scene::Mesh> plate = find_mesh(reloaded.data, "plate");
    ASSERT_TRUE(plate.operator bool());
    EXPECT_FALSE(plate->read_local_value(erhe::scene::Gprim::double_sided_property).has_value());

    const std::shared_ptr<erhe::scene::Mesh> single = find_mesh(reloaded.data, "single");
    ASSERT_TRUE(single.operator bool());
    EXPECT_EQ(single->read_local_value(erhe::scene::Gprim::double_sided_property), std::optional<bool>{false});
}

TEST_F(Double_sided_round_trip, the_second_save_is_byte_identical)
{
    EXPECT_EQ(read_text(rewritten_path), read_text(written_path));
}
