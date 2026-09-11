// USD `holeIndices` names the faces a renderer does not draw. Tydra removes
// them only when it triangulates, and the conversion keeps the authored
// polygons, so the importer drops the hole facets itself: they are claimed
// before any subset sees them and land in no facet group, which is what both
// the geometry-normative build and the triangle-soup build walk (usd-wg
// full_assets/SubdivisionSurfaces authors four of them on each pyramid).

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
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

class Hole_faces : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments arguments{
            .path          = test_data_path("hole_faces.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(arguments);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

// Two quads with the second one a hole: the geometry-normative build keeps
// one facet.
TEST_F(Hole_faces, a_hole_facet_is_not_in_the_normative_geometry)
{
    const erhe::scene::Mesh* mesh = find_mesh(root, "normative");
    ASSERT_NE(mesh, nullptr);
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    ASSERT_EQ(primitives.size(), 1u);
    ASSERT_TRUE(primitives.front().primitive.operator bool());
    ASSERT_TRUE(primitives.front().primitive->render_shape.operator bool());
    const std::shared_ptr<erhe::geometry::Geometry> geometry = primitives.front().primitive->render_shape->get_geometry_const();
    ASSERT_TRUE(geometry.operator bool());
    EXPECT_EQ(geometry->get_mesh().facets.nb(), 1u);
}

// The same on a subdivision cage, which the conversion imports as a triangle
// soup: one quad fans into two triangles, the hole quad into none.
TEST_F(Hole_faces, a_hole_facet_is_not_in_the_triangle_soup)
{
    const erhe::scene::Mesh* mesh = find_mesh(root, "cage");
    ASSERT_NE(mesh, nullptr);
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    ASSERT_EQ(primitives.size(), 1u);
    ASSERT_TRUE(primitives.front().primitive.operator bool());
    ASSERT_TRUE(primitives.front().primitive->render_shape.operator bool());
    const std::shared_ptr<erhe::primitive::Triangle_soup> soup = primitives.front().primitive->render_shape->get_triangle_soup();
    ASSERT_TRUE(soup.operator bool());
    EXPECT_EQ(soup->index_data.size(), 6u);
}

} // anonymous namespace
