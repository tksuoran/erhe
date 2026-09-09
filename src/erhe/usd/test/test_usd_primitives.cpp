// The UsdGeom primitive schemas (doc/usd-compatibility-plan.md S1): a
// `Cube`, `Sphere`, `Cone`, `Cylinder`, `Capsule` or `Cylinder_1` prim is the
// mesh its schema attributes describe, and it saves back as the `Mesh` it is.

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_primitive_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] auto find_prim(const std::shared_ptr<erhe::Hierarchy>& root, const std::string& name) -> std::shared_ptr<erhe::Hierarchy>
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : root->get_children()) {
        if (child->get_name() == name) {
            return child;
        }
        const std::shared_ptr<erhe::Hierarchy> found = find_prim(child, name);
        if (found) {
            return found;
        }
    }
    return {};
}

[[nodiscard]] auto find_mesh(const std::shared_ptr<erhe::Hierarchy>& root, const std::string& name) -> const erhe::scene::Mesh*
{
    const std::shared_ptr<erhe::Hierarchy> prim = find_prim(root, name);
    if (!prim || !erhe::is<erhe::scene::Mesh>(prim.get())) {
        return nullptr;
    }
    return static_cast<const erhe::scene::Mesh*>(prim.get());
}

[[nodiscard]] auto geometry_of(const erhe::scene::Mesh& mesh) -> std::shared_ptr<erhe::geometry::Geometry>
{
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
    if (primitives.size() != 1) {
        return {};
    }
    if (!primitives.front().primitive || !primitives.front().primitive->render_shape) {
        return {};
    }
    return primitives.front().primitive->render_shape->get_geometry_const();
}

[[nodiscard]] auto facet_count(const erhe::scene::Mesh& mesh) -> std::size_t
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = geometry_of(mesh);
    return geometry ? geometry->get_mesh().facets.nb() : 0;
}

// The half extent of the mesh's points along one axis, in the mesh's own
// local space.
[[nodiscard]] auto half_extent(const erhe::scene::Mesh& mesh, const int axis) -> float
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = geometry_of(mesh);
    if (!geometry) {
        return 0.0f;
    }
    const GEO::Mesh& geo_mesh = geometry->get_mesh();
    float            result   = 0.0f;
    for (GEO::index_t vertex = 0; vertex < geo_mesh.vertices.nb(); ++vertex) {
        const float* point = geo_mesh.vertices.single_precision_point_ptr(vertex);
        result = std::max(result, std::abs(point[axis]));
    }
    return result;
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

class Primitive_schema_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("primitives.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

// Every primitive schema of the fixture is an erhe Mesh prim with geometry.
TEST_F(Primitive_schema_import, every_schema_becomes_a_mesh_with_facets)
{
    const char* const names[] = {"Box", "Ball", "Spike", "Pipe", "Pill", "Taper"};
    for (const char* const name : names) {
        const erhe::scene::Mesh* mesh = find_mesh(root, name);
        ASSERT_NE(mesh, nullptr) << name;
        EXPECT_EQ(mesh->get_class_type_name(), "Mesh") << name;
        // One primitive: a primitive schema holds no GeomSubset.
        EXPECT_EQ(mesh->get_primitives().size(), 1u) << name;
        EXPECT_GT(facet_count(*mesh), 0u) << name;
        // The geometry is normative, the way a `subdivisionScheme = none`
        // mesh is, so the prim carries a Geometry with edges.
        const std::shared_ptr<erhe::geometry::Geometry> geometry = geometry_of(*mesh);
        ASSERT_TRUE(geometry.operator bool()) << name;
        EXPECT_GT(geometry->get_mesh().edges.nb(), 0u) << name;
    }
}

// A `Cube` of size 2 spans [-1, 1] on every axis.
TEST_F(Primitive_schema_import, cube_size_is_the_edge_length)
{
    const erhe::scene::Mesh* box = find_mesh(root, "Box");
    ASSERT_NE(box, nullptr);
    EXPECT_NEAR(half_extent(*box, 0), 1.0f, 1e-4f);
    EXPECT_NEAR(half_extent(*box, 1), 1.0f, 1e-4f);
    EXPECT_NEAR(half_extent(*box, 2), 1.0f, 1e-4f);
}

TEST_F(Primitive_schema_import, sphere_radius_is_the_extent)
{
    const erhe::scene::Mesh* ball = find_mesh(root, "Ball");
    ASSERT_NE(ball, nullptr);
    EXPECT_NEAR(half_extent(*ball, 0), 1.5f, 1e-3f);
    EXPECT_NEAR(half_extent(*ball, 1), 1.5f, 1e-3f);
    EXPECT_NEAR(half_extent(*ball, 2), 1.5f, 1e-3f);
}

// The `axis` token is baked into the geometry: a `Cylinder` of height 3 with
// `axis = "Y"` is 3 long on Y and 1 wide on X and Z.
TEST_F(Primitive_schema_import, cylinder_axis_y_is_upright)
{
    const erhe::scene::Mesh* pipe = find_mesh(root, "Pipe");
    ASSERT_NE(pipe, nullptr);
    EXPECT_NEAR(half_extent(*pipe, 1), 1.5f, 1e-3f);
    EXPECT_NEAR(half_extent(*pipe, 0), 0.5f, 1e-3f);
    EXPECT_NEAR(half_extent(*pipe, 2), 0.5f, 1e-3f);
}

// The schema default axis is Z, and the fixture spells it out.
TEST_F(Primitive_schema_import, cone_axis_z_runs_along_z)
{
    const erhe::scene::Mesh* spike = find_mesh(root, "Spike");
    ASSERT_NE(spike, nullptr);
    EXPECT_NEAR(half_extent(*spike, 2), 1.0f, 1e-3f);
    EXPECT_NEAR(half_extent(*spike, 0), 0.5f, 1e-3f);
}

// A `Capsule` of height 1 and radius 0.4 is 1 + 2 * 0.4 long on its axis.
TEST_F(Primitive_schema_import, capsule_height_is_the_mid_section)
{
    const erhe::scene::Mesh* pill = find_mesh(root, "Pill");
    ASSERT_NE(pill, nullptr);
    EXPECT_NEAR(half_extent(*pill, 2), 0.9f, 1e-3f);
    EXPECT_NEAR(half_extent(*pill, 0), 0.4f, 1e-3f);
}

// `Cylinder_1` carries a radius per end, which is a conical frustum.
TEST_F(Primitive_schema_import, cylinder_1_radii_taper)
{
    const erhe::scene::Mesh* taper = find_mesh(root, "Taper");
    ASSERT_NE(taper, nullptr);
    const std::shared_ptr<erhe::geometry::Geometry> geometry = geometry_of(*taper);
    ASSERT_TRUE(geometry.operator bool());
    const GEO::Mesh& geo_mesh    = geometry->get_mesh();
    float            top_radius  = 0.0f;
    float            bottom_radius = 0.0f;
    for (GEO::index_t vertex = 0; vertex < geo_mesh.vertices.nb(); ++vertex) {
        const float* point  = geo_mesh.vertices.single_precision_point_ptr(vertex);
        const float  radius = std::sqrt((point[0] * point[0]) + (point[2] * point[2]));
        if (point[1] > 0.5f) {
            top_radius = std::max(top_radius, radius);
        } else if (point[1] < -0.5f) {
            bottom_radius = std::max(bottom_radius, radius);
        }
    }
    EXPECT_NEAR(top_radius,    0.25f, 1e-3f);
    EXPECT_NEAR(bottom_radius, 1.0f,  1e-3f);
}

// The prim's own xformOps are its transform, for a schema the render scene
// carries (`Cube`) and for one it does not (`Cylinder_1`) alike.
TEST_F(Primitive_schema_import, authored_xform_ops_place_the_prim)
{
    const erhe::scene::Mesh* box = find_mesh(root, "Box");
    ASSERT_NE(box, nullptr);
    const glm::vec4 box_origin = box->world_from_node() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_NEAR(box_origin.x, -6.0f, 1e-4f);

    const erhe::scene::Mesh* taper = find_mesh(root, "Taper");
    ASSERT_NE(taper, nullptr);
    const glm::vec4 taper_origin = taper->world_from_node() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_NEAR(taper_origin.x, 8.0f, 1e-4f);
}

// A primitive-schema prim binds a material the way any Gprim does.
// `primvars:displayColor` / `displayOpacity` on a schema prim color every
// corner of the generated geometry, the way a `Mesh` prim's primvars do; a
// schema prim without them carries no corner color.
TEST_F(Primitive_schema_import, display_color_reaches_every_corner)
{
    const erhe::scene::Mesh* box = find_mesh(root, "Box");
    ASSERT_NE(box, nullptr);
    const std::shared_ptr<erhe::geometry::Geometry> geometry = geometry_of(*box);
    ASSERT_TRUE(geometry.operator bool());
    const GEO::Mesh& geo_mesh = geometry->get_mesh();
    ASSERT_GT(geo_mesh.facet_corners.nb(), 0u);
    for (GEO::index_t corner = 0; corner < geo_mesh.facet_corners.nb(); ++corner) {
        const std::optional<GEO::vec4f> color = geometry->get_attributes().corner_color_0.try_get(corner);
        ASSERT_TRUE(color.has_value()) << "corner " << corner;
        EXPECT_NEAR(color.value().x, 0.0f, 1e-6f);
        EXPECT_NEAR(color.value().y, 0.0f, 1e-6f);
        EXPECT_NEAR(color.value().z, 0.8f, 1e-6f);
        EXPECT_NEAR(color.value().w, 0.5f, 1e-6f);
    }

    const erhe::scene::Mesh* ball = find_mesh(root, "Ball");
    ASSERT_NE(ball, nullptr);
    const std::shared_ptr<erhe::geometry::Geometry> ball_geometry = geometry_of(*ball);
    ASSERT_TRUE(ball_geometry.operator bool());
    EXPECT_FALSE(ball_geometry->get_attributes().corner_color_0.try_get(0).has_value());
}

TEST_F(Primitive_schema_import, material_binding_reaches_the_primitive)
{
    const erhe::scene::Mesh* pipe = find_mesh(root, "Pipe");
    ASSERT_NE(pipe, nullptr);
    ASSERT_EQ(pipe->get_primitives().size(), 1u);
    const std::shared_ptr<erhe::primitive::Material>& material = pipe->get_primitives().front().material;
    ASSERT_TRUE(material.operator bool());
    EXPECT_EQ(material->get_name(), "Red");
}

class Primitive_schema_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        source      = load(test_data_path("primitives.usda"), source_root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("primitives.usda");
        const erhe::usd::Usd_save_arguments save_arguments{
            .path      = written_path,
            .root_node = source_root,
            .materials = source.data.materials
        };
        const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;
        written = read_file(written_path);
    }

    std::shared_ptr<erhe::scene::Node> source_root;
    std::filesystem::path              written_path;
    std::string                        written;
    erhe::usd::Usd_load_result         source;
};

// The item is a Mesh, so the save writes the mesh it describes: the file's
// `Cube` spelling is not kept.
TEST_F(Primitive_schema_round_trip, a_schema_prim_saves_as_a_mesh)
{
    EXPECT_NE(written.find("def Mesh \"Box\""), std::string::npos) << written;
    EXPECT_NE(written.find("def Mesh \"Pipe\""), std::string::npos) << written;
    EXPECT_EQ(written.find("def Cube"), std::string::npos) << written;
    EXPECT_EQ(written.find("def Cylinder"), std::string::npos) << written;
    EXPECT_NE(written.find("point3f[] points"), std::string::npos) << written;
}

// The first save changes representation - a schema prim becomes the mesh it
// describes, in the generator's own vertex order, while a reload rebuilds
// that mesh in the importer's order - so the round trip is a fixed point from
// the first RELOAD on: save two and save three are byte-identical.
TEST_F(Primitive_schema_round_trip, the_round_trip_settles_after_the_first_reload)
{
    std::shared_ptr<erhe::scene::Node> second_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_result   second_load = load(written_path, second_root);
    ASSERT_TRUE(second_load.error.empty()) << second_load.error;

    const std::filesystem::path        second_path = temporary_path("primitives_second.usda");
    const erhe::usd::Usd_save_arguments second_save_arguments{
        .path      = second_path,
        .root_node = second_root,
        .materials = second_load.data.materials
    };
    const erhe::usd::Usd_save_result second_save = erhe::usd::save_usda(second_save_arguments);
    ASSERT_TRUE(second_save.error.empty()) << second_save.error;

    std::shared_ptr<erhe::scene::Node> third_root = std::make_shared<erhe::scene::Xform>("reload_root_2");
    const erhe::usd::Usd_load_result   third_load = load(second_path, third_root);
    ASSERT_TRUE(third_load.error.empty()) << third_load.error;

    const std::filesystem::path        third_path = temporary_path("primitives_third.usda");
    const erhe::usd::Usd_save_arguments third_save_arguments{
        .path      = third_path,
        .root_node = third_root,
        .materials = third_load.data.materials
    };
    const erhe::usd::Usd_save_result third_save = erhe::usd::save_usda(third_save_arguments);
    ASSERT_TRUE(third_save.error.empty()) << third_save.error;

    EXPECT_EQ(read_file(third_path), read_file(second_path));
}

} // anonymous namespace
