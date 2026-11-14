#include "scene/navigation_visualization.hpp"

#include "app_context.hpp"
#include "editor_item_bits.hpp"
#include "editor_log.hpp"
#include "navigation/navigation_mesh.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item_flags.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <geogram/mesh/mesh.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

namespace {

// Root-space Y offset above the walkable surface, so the polygons do not z-fight the floor.
constexpr float c_navigation_visualization_lift = 0.05f;

enum class Navigation_polygon_class : unsigned int {
    reachable = 0,
    unreachable
};

class Navigation_class_style
{
public:
    const char* mesh_name;
    const char* material_name;
    glm::vec3   fill_color;
    uint64_t    flag_bits;
};

[[nodiscard]] auto get_class_style(const Navigation_polygon_class polygon_class) -> Navigation_class_style
{
    switch (polygon_class) {
        case Navigation_polygon_class::reachable: {
            return Navigation_class_style{
                .mesh_name     = "Navigation Mesh Reachable",
                .material_name = "Navigation Reachable",
                .fill_color    = glm::vec3{0.10f, 0.80f, 0.25f},
                .flag_bits     = Editor_item_flags::navigation_mesh | Editor_item_flags::navigation_reachable
            };
        }
        case Navigation_polygon_class::unreachable:
        default: {
            return Navigation_class_style{
                .mesh_name     = "Navigation Mesh Unreachable",
                .material_name = "Navigation Unreachable",
                .fill_color    = glm::vec3{0.90f, 0.15f, 0.60f},
                .flag_bits     = Editor_item_flags::navigation_mesh
            };
        }
    }
}

[[nodiscard]] auto is_in_class(const std::span<const uint8_t> reachable, const std::size_t polygon, const Navigation_polygon_class polygon_class) -> bool
{
    const bool polygon_reachable = (polygon < reachable.size()) && (reachable[polygon] != 0);
    return polygon_reachable == (polygon_class == Navigation_polygon_class::reachable);
}

[[nodiscard]] auto make_vertex_key(const glm::vec3& p) -> std::array<uint32_t, 3>
{
    return std::array<uint32_t, 3>{std::bit_cast<uint32_t>(p.x), std::bit_cast<uint32_t>(p.y), std::bit_cast<uint32_t>(p.z)};
}

class Vertex_key_hash
{
public:
    [[nodiscard]] auto operator()(const std::array<uint32_t, 3>& key) const -> std::size_t
    {
        return (static_cast<std::size_t>(key[0]) * 73856093u) ^ (static_cast<std::size_t>(key[1]) * 19349663u) ^ (static_cast<std::size_t>(key[2]) * 83492791u);
    }
};

// One facet edge of the class geometry: the polygon of its first facet and whether every facet of it is of that polygon.
class Class_edge
{
public:
    std::size_t polygon     {0};
    uint32_t    facet_count {0};
    bool        one_polygon {true};
};

// The detail triangles of the class's polygons over bit-equal welded vertices, with mesh edges only where a
// polygon border runs (an edge two triangles of one polygon share is interior); null when the class has none.
[[nodiscard]] auto make_class_geometry(
    const Navigation_mesh&         mesh,
    const std::span<const uint8_t> reachable,
    const Navigation_polygon_class polygon_class
) -> std::shared_ptr<erhe::geometry::Geometry>
{
    const std::span<const Navigation_polygon> polygons        = mesh.get_polygons();
    const std::span<const glm::vec3>          detail_vertices = mesh.get_detail_triangle_vertices();
    std::shared_ptr<erhe::geometry::Geometry> geometry        = std::make_shared<erhe::geometry::Geometry>(get_class_style(polygon_class).mesh_name);
    GEO::Mesh&                                geo_mesh        = geometry->get_mesh();

    std::unordered_map<std::array<uint32_t, 3>, GEO::index_t, Vertex_key_hash> vertex_by_key;
    std::unordered_map<uint64_t, Class_edge>                                   edges;
    std::vector<uint64_t>                                                      edge_order; // first-seen order, so the result does not depend on hashing
    const auto vertex = [&](const glm::vec3& p) -> GEO::index_t {
        const auto [i, inserted] = vertex_by_key.try_emplace(make_vertex_key(p), 0);
        if (inserted) {
            i->second = geo_mesh.vertices.create_vertex();
            erhe::geometry::set_pointf(geo_mesh.vertices, i->second, GEO::vec3f{p.x, p.y, p.z});
        }
        return i->second;
    };
    for (std::size_t polygon_index = 0, end = polygons.size(); polygon_index < end; ++polygon_index) {
        if (!is_in_class(reachable, polygon_index, polygon_class)) {
            continue;
        }
        const Navigation_polygon& polygon = polygons[polygon_index];
        for (uint32_t t = 0; t < polygon.detail_triangle_count; ++t) {
            const std::size_t first = 3 * (static_cast<std::size_t>(polygon.first_detail_triangle) + t);
            const std::array<GEO::index_t, 3> corners{vertex(detail_vertices[first]), vertex(detail_vertices[first + 1]), vertex(detail_vertices[first + 2])};
            if ((corners[0] == corners[1]) || (corners[1] == corners[2]) || (corners[2] == corners[0])) {
                continue;
            }
            geo_mesh.facets.create_triangle(corners[0], corners[1], corners[2]);
            for (std::size_t c = 0; c < 3; ++c) {
                const GEO::index_t a   = corners[c];
                const GEO::index_t b   = corners[(c + 1) % 3];
                const uint64_t     key = (static_cast<uint64_t>(std::min(a, b)) << 32) | static_cast<uint64_t>(std::max(a, b));
                const auto [i, inserted] = edges.try_emplace(key, Class_edge{.polygon = polygon_index});
                if (inserted) {
                    edge_order.push_back(key);
                }
                Class_edge& edge = i->second;
                edge.one_polygon = edge.one_polygon && (edge.polygon == polygon_index);
                ++edge.facet_count;
            }
        }
    }
    if (geo_mesh.facets.nb() == 0) {
        return {};
    }
    for (const uint64_t key : edge_order) {
        const Class_edge& edge = edges.at(key);
        if (edge.one_polygon && (edge.facet_count == 2)) {
            continue;
        }
        geo_mesh.edges.create_edge(static_cast<GEO::index_t>(key >> 32), static_cast<GEO::index_t>(key & 0xffffffffu));
    }
    return geometry;
}

[[nodiscard]] auto make_primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const erhe::primitive::Build_info&               build_info
) -> std::shared_ptr<erhe::primitive::Primitive>
{
    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(geometry);
    if (!primitive->make_renderable_mesh(build_info, erhe::primitive::Normal_style::polygon_normals)) {
        log_navigation->warn("Navigation mesh visualization: '{}' did not build a renderable mesh", geometry->get_name());
        return {};
    }
    return primitive;
}

[[nodiscard]] auto make_class_mesh(
    App_context&                   context,
    const Navigation_mesh&         mesh,
    const std::span<const uint8_t> reachable,
    const Navigation_polygon_class polygon_class
) -> std::shared_ptr<erhe::scene::Mesh>
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = make_class_geometry(mesh, reachable, polygon_class);
    if (!geometry) {
        return {};
    }
    // Fill triangles for the polygons, edge lines for their borders.
    const std::shared_ptr<erhe::primitive::Primitive> primitive = make_primitive(
        geometry,
        erhe::primitive::Build_info{
            .primitive_types{.fill_triangles = true, .edge_lines = true},
            .buffer_info = context.mesh_memory->make_primitive_buffer_info()
        }
    );
    if (!primitive) {
        return {};
    }

    const Navigation_class_style style = get_class_style(polygon_class);
    std::shared_ptr<erhe::primitive::Material> material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name   = style.material_name,
            .values = {
                .base_color    = style.fill_color,
                .opacity       = 0.45f,
                .bxdf_model    = erhe::primitive::Bxdf_model::unlit,
                .blending_mode = erhe::primitive::Material_blending_mode::alpha_blend,
                .double_sided  = true
            }
        }
    );
    material->enable_flag_bits(erhe::Item_flags::session_only);
    material->disable_flag_bits(erhe::Item_flags::show_in_ui);

    std::shared_ptr<erhe::scene::Mesh> class_mesh = std::make_shared<erhe::scene::Mesh>(style.mesh_name);
    class_mesh->add_primitive(primitive, material);
    class_mesh->layer_id = Mesh_layer_id::navigation;
    class_mesh->set_value(erhe::scene::Mesh::shadow_cast_property, false);
    class_mesh->enable_flag_bits(erhe::Item_flags::render_proxy | erhe::Item_flags::session_only | style.flag_bits);
    class_mesh->disable_flag_bits(erhe::Item_flags::show_in_ui);
    // The material sits under its mesh, which gives it a home without a content library entry (as draw mode cards do).
    material->set_parent(class_mesh);
    return class_mesh;
}

} // anonymous namespace

auto make_navigation_visualization(
    App_context&                   context,
    const Navigation_mesh&         mesh,
    const std::span<const uint8_t> reachable
) -> std::shared_ptr<erhe::scene::Node>
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<erhe::scene::Mesh> reachable_mesh   = make_class_mesh(context, mesh, reachable, Navigation_polygon_class::reachable);
    const std::shared_ptr<erhe::scene::Mesh> unreachable_mesh = make_class_mesh(context, mesh, reachable, Navigation_polygon_class::unreachable);
    if (!reachable_mesh && !unreachable_mesh) {
        return {};
    }
    const std::shared_ptr<erhe::scene::Node> group = std::make_shared<erhe::scene::Xform>("Navigation Mesh");
    group->enable_flag_bits(
        erhe::Item_flags::render_proxy         |
        erhe::Item_flags::session_only         |
        Editor_item_flags::navigation_mesh     |
        Editor_item_flags::lock_viewport_transform
    );
    group->disable_flag_bits(erhe::Item_flags::show_in_ui);
    group->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, c_navigation_visualization_lift, 0.0f}));
    if (reachable_mesh) {
        erhe::scene::set_mesh_parent(reachable_mesh, group);
    }
    if (unreachable_mesh) {
        erhe::scene::set_mesh_parent(unreachable_mesh, group);
    }
    return group;
}

}
