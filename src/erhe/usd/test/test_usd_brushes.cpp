// Brushes as `Brush`-typed prims (doc/usd-compatibility-plan.md E4a). The
// reader records every `Brush` prim of the root layer with its geometry, its
// density, its normal style and the material it binds, and stops the scene
// conversion at it, so the brush geometry is no mesh of the scene; the writer
// puts a brush item back as the same prim with its `Mesh` child. erhe::usd
// creates no Brush item - that is the editor's half of the step - so the
// export side builds a stand-in prim carrying the class token a brush has,
// and hands the writer what the brush holds.

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

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
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_brush_tests";
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
        lines.push_back(line);
    }
    return lines;
}

[[nodiscard]] auto has_line_with(const std::vector<std::string>& lines, const std::string& needle) -> bool
{
    for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// The lines of the prim block starting at the first line holding `needle`,
// down to the line whose indentation returns to that of the opening line: the
// `Brushes` scope of a written file, which two saves must spell identically.
[[nodiscard]] auto block_lines(const std::vector<std::string>& lines, const std::string& needle) -> std::vector<std::string>
{
    std::vector<std::string> block;
    std::size_t              index = 0;
    while ((index < lines.size()) && (lines[index].find(needle) == std::string::npos)) {
        ++index;
    }
    if (index >= lines.size()) {
        return block;
    }
    const std::size_t indent = lines[index].find_first_not_of(' ');
    block.push_back(lines[index]);
    for (++index; index < lines.size(); ++index) {
        block.push_back(lines[index]);
        const std::size_t line_indent = lines[index].find_first_not_of(' ');
        if ((line_indent == indent) && (lines[index].find('}') != std::string::npos)) {
            break;
        }
    }
    return block;
}

[[nodiscard]] auto find_brush(
    const erhe::usd::Usd_data& data,
    const std::string&         stage_path
) -> const erhe::usd::Usd_brush_prim*
{
    for (const erhe::usd::Usd_brush_prim& brush : data.brushes) {
        if (brush.stage_path == stage_path) {
            return &brush;
        }
    }
    return nullptr;
}

// A stand-in for editor::Brush: a prim whose class token is the one the
// exporter recognizes a brush by. erhe::usd cannot name the editor type, and
// the token is exactly what it goes on (usd_export.cpp).
class Test_brush : public erhe::Item<erhe::Item_base, erhe::Typed, Test_brush>
{
public:
    explicit Test_brush(const std::string_view name) : Item{name}
    {
        enable_flag_bits(erhe::Item_flags::show_in_ui);
    }
    explicit Test_brush(const Test_brush& other) = default;

    static constexpr std::string_view static_type_name{"Brush"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Typed::get_static_type() | erhe::Item_type::brush;
    }
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Brush"; }
};

[[nodiscard]] auto make_box_geometry(const std::string& name) -> std::shared_ptr<erhe::geometry::Geometry>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>(name);
    erhe::geometry::shapes::make_box(geometry->get_mesh(), -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
    geometry->process({.flags = erhe::geometry::Geometry::process_flag_connect});
    return geometry;
}

class Brushes_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("brushes.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        loaded = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         loaded;
};

TEST_F(Brushes_import, a_brush_prim_is_recorded_with_its_geometry_and_its_values)
{
    const erhe::usd::Usd_brush_prim* quad = find_brush(loaded.data, "/World/Brushes/Quad");
    ASSERT_NE(quad, nullptr);
    EXPECT_EQ(quad->name, "Quad");
    EXPECT_FLOAT_EQ(quad->density, 2.5f);
    EXPECT_EQ(quad->normal_style, "polygon_normals");
    EXPECT_EQ(quad->material_path, "/World/Looks/Copper");
    ASSERT_TRUE(quad->geometry);
    EXPECT_EQ(quad->geometry->get_mesh().vertices.nb(), 4u);
    EXPECT_EQ(quad->geometry->get_mesh().facets.nb(), 1u);
}

TEST_F(Brushes_import, a_brush_without_a_material_binding_records_none)
{
    const erhe::usd::Usd_brush_prim* triangle = find_brush(loaded.data, "/World/Brushes/Triangle");
    ASSERT_NE(triangle, nullptr);
    EXPECT_FLOAT_EQ(triangle->density, 1.0f);
    EXPECT_TRUE(triangle->normal_style.empty());
    EXPECT_TRUE(triangle->material_path.empty());
    ASSERT_TRUE(triangle->geometry);
    EXPECT_EQ(triangle->geometry->get_mesh().vertices.nb(), 3u);
    EXPECT_EQ(triangle->geometry->get_mesh().facets.nb(), 1u);
}

TEST_F(Brushes_import, a_brush_prim_without_a_geometry_child_is_one_warning_and_no_brush)
{
    EXPECT_EQ(loaded.data.brushes.size(), 2u);
    EXPECT_EQ(find_brush(loaded.data, "/World/Brushes/Empty"), nullptr);
    EXPECT_NE(loaded.warning.find("/World/Brushes/Empty"), std::string::npos) << loaded.warning;
}

TEST_F(Brushes_import, a_brush_geometry_is_no_scene_content)
{
    for (const std::shared_ptr<erhe::scene::Node>& node : loaded.data.nodes) {
        ASSERT_TRUE(node);
        EXPECT_NE(node->get_name(), "geometry");
        EXPECT_NE(node->get_name(), "Quad");
        EXPECT_NE(node->get_name(), "Triangle");
    }
    for (const std::shared_ptr<erhe::Typed>& prim : loaded.data.prims) {
        ASSERT_TRUE(prim);
        EXPECT_NE(prim->get_name(), "Quad");
        EXPECT_NE(prim->get_name(), "Triangle");
        EXPECT_NE(prim->get_name(), "Empty");
    }
}

class Brushes_export : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("save_root");

        std::shared_ptr<erhe::scene::Xform> world = std::make_shared<erhe::scene::Xform>("World");
        world->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        world->set_parent(root);

        std::shared_ptr<erhe::Scope> looks_scope = std::make_shared<erhe::Scope>("Looks");
        looks_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
        looks_scope->set_parent(world);

        material = std::make_shared<erhe::primitive::Material>(
            erhe::primitive::Material_create_info{.name = "Copper"}
        );
        material->set_parent(looks_scope);

        std::shared_ptr<erhe::Scope> brushes_scope = std::make_shared<erhe::Scope>("Brushes");
        brushes_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
        brushes_scope->set_parent(world);

        bound = std::make_shared<Test_brush>("Bound");
        bound->set_parent(brushes_scope);
        plain = std::make_shared<Test_brush>("Plain");
        plain->set_parent(brushes_scope);

        geometry = make_box_geometry("box");

        written_path = temporary_path("brushes.usda");
        save_arguments.path      = written_path;
        save_arguments.root_node = root;
        save_arguments.materials = std::vector<std::shared_ptr<erhe::primitive::Material>>{material};
        save_arguments.brushes   = std::vector<erhe::usd::Usd_save_brush>{
            erhe::usd::Usd_save_brush{
                .item         = bound,
                .geometry     = geometry,
                .density      = 2.5f,
                .normal_style = "polygon_normals",
                .material     = material
            },
            erhe::usd::Usd_save_brush{
                .item         = plain,
                .geometry     = geometry,
                .density      = 1.0f,
                .normal_style = "corner_normals",
                .material     = {}
            }
        };
        save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;
        lines = read_lines(written_path);
    }

    std::shared_ptr<erhe::scene::Node>                root;
    std::shared_ptr<erhe::primitive::Material>        material;
    std::shared_ptr<Test_brush>                       bound;
    std::shared_ptr<Test_brush>                       plain;
    std::shared_ptr<erhe::geometry::Geometry>         geometry;
    std::filesystem::path                             written_path;
    erhe::usd::Usd_save_arguments                     save_arguments;
    erhe::usd::Usd_save_result                        save;
    std::vector<std::string>                          lines;
};

TEST_F(Brushes_export, a_brush_item_is_written_as_a_brush_prim_with_a_geometry_child)
{
    EXPECT_TRUE(has_line_with(lines, "def Brush \"Bound\""));
    EXPECT_TRUE(has_line_with(lines, "def Brush \"Plain\""));
    EXPECT_TRUE(has_line_with(lines, "def Mesh \"geometry\""));
}

TEST_F(Brushes_export, a_brush_writes_its_density_normal_style_and_material_binding)
{
    EXPECT_TRUE(has_line_with(lines, "custom float erhe:Brush:density = 2.5"));
    EXPECT_TRUE(has_line_with(lines, "custom token erhe:Brush:normal_style = \"polygon_normals\""));
    EXPECT_TRUE(has_line_with(lines, "rel material:binding = </World/Looks/Copper>"));
}

TEST_F(Brushes_export, a_brush_geometry_is_written_as_authored_polygons)
{
    EXPECT_TRUE(has_line_with(lines, "uniform token subdivisionScheme = \"none\""));
    EXPECT_TRUE(has_line_with(lines, "int[] faceVertexCounts"));
    EXPECT_TRUE(has_line_with(lines, "point3f[] points"));
}

TEST_F(Brushes_export, a_second_save_writes_the_same_brushes_block)
{
    const std::vector<std::string> first = block_lines(lines, "def Scope \"Brushes\"");
    EXPECT_FALSE(first.empty());

    const std::filesystem::path second_path = temporary_path("brushes_2.usda");
    erhe::usd::Usd_save_arguments second_arguments = save_arguments;
    second_arguments.path = second_path;
    const erhe::usd::Usd_save_result second = erhe::usd::save_usda(second_arguments);
    ASSERT_TRUE(second.error.empty()) << second.error;
    const std::vector<std::string> second_lines = read_lines(second_path);
    EXPECT_EQ(block_lines(second_lines, "def Scope \"Brushes\""), first);
    EXPECT_EQ(second_lines, lines);
}

// The prims a brush is written as read back as the brush records they were.
TEST_F(Brushes_export, the_written_brush_prims_read_back)
{
    std::shared_ptr<erhe::scene::Node>  reload_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_arguments load_arguments{
        .path          = written_path,
        .root_node     = reload_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

    const erhe::usd::Usd_brush_prim* reloaded_bound = find_brush(reloaded.data, "/World/Brushes/Bound");
    ASSERT_NE(reloaded_bound, nullptr);
    EXPECT_FLOAT_EQ(reloaded_bound->density, 2.5f);
    EXPECT_EQ(reloaded_bound->normal_style, "polygon_normals");
    EXPECT_EQ(reloaded_bound->material_path, "/World/Looks/Copper");
    ASSERT_TRUE(reloaded_bound->geometry);
    EXPECT_EQ(reloaded_bound->geometry->get_mesh().vertices.nb(), geometry->get_mesh().vertices.nb());
    EXPECT_EQ(reloaded_bound->geometry->get_mesh().facets.nb(), geometry->get_mesh().facets.nb());

    const erhe::usd::Usd_brush_prim* reloaded_plain = find_brush(reloaded.data, "/World/Brushes/Plain");
    ASSERT_NE(reloaded_plain, nullptr);
    EXPECT_TRUE(reloaded_plain->material_path.empty());
}

} // anonymous namespace
