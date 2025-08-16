// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "parsers/geogram.hpp"

#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>

#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_io.h>

namespace editor {

void import_geogram(
    erhe::primitive::Build_info  build_info,
    Scene_root&                  scene_root,
    const std::filesystem::path& path
)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};

    // Destination
    erhe::scene::Scene* scene = scene_root.get_hosted_scene();
    const auto scene_root_node = scene->get_root_node();
    std::shared_ptr<Content_library> content_library = scene_root.get_content_library();

    // Just pick one/any material for now
    std::shared_ptr<erhe::primitive::Material> material{};
    content_library->materials->for_each<Content_library_node>(
        [&material](const Content_library_node& node) {
            auto entry = std::dynamic_pointer_cast<erhe::primitive::Material>(node.item);
            if (entry) {
                material = entry;
                return false;
            }
            return true;
        }
    );

    GEO::MeshIOFlags ioFlags{};
    ioFlags.set_dimension(3);
    ioFlags.set_attributes(GEO::MeshAttributesFlags::MESH_ALL_ATTRIBUTES);
    ioFlags.set_elements(GEO::MeshElementsFlags::MESH_ALL_ELEMENTS); // | MESH_ALL_SUBELEMENTS?
    const std::string path_string = path.string();
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>();
    GEO::Mesh& geo_mesh = geometry->get_mesh();
    bool load_ok = GEO::mesh_load(path_string.c_str(), geo_mesh, ioFlags);
    if (!load_ok) {
        return;
    }
    if ((geo_mesh.vertices.nb() == 0) || (geo_mesh.facets.nb() == 0)) {
        return;
    }

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;

    geo_mesh.vertices.set_single_precision();
    geometry->process(flags);

    constexpr uint64_t mesh_flags =
        erhe::Item_flags::visible     |
        erhe::Item_flags::content     |
        erhe::Item_flags::opaque      |
        erhe::Item_flags::shadow_cast |
        erhe::Item_flags::id          |
        erhe::Item_flags::show_in_ui;
    constexpr uint64_t node_flags =
        erhe::Item_flags::visible     |
        erhe::Item_flags::content     |
        erhe::Item_flags::show_in_ui;
    constexpr erhe::primitive::Normal_style normal_style = erhe::primitive::Normal_style::corner_normals;

    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(
        geometry,
        build_info,
        normal_style
    );
    const bool raytrace_ok = primitive->make_raytrace();
    static_cast<void>(raytrace_ok);

    auto node = std::make_shared<erhe::scene::Node>(path_string);
    auto mesh = std::make_shared<erhe::scene::Mesh>(path_string);
    mesh->add_primitive(primitive, material);

    mesh->layer_id = scene_root.layers().content()->id;
    mesh->enable_flag_bits(mesh_flags);
    node->attach          (mesh);
    node->enable_flag_bits(node_flags);
    node->set_world_from_node(
        erhe::scene::Trs_transform(glm::vec3{0.0f, 0.1f, 0.0f})
    );
    node->set_parent(scene_root_node);
}

}
