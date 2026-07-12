#include "erhe_geometry/geometry_serialization.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/basic/attributes.h>
#include <geogram/mesh/mesh.h>

#include <cstring>

namespace erhe::geometry {

namespace {

// Attributes handled structurally (not part of the generic dump): vertex
// positions ("point" double precision / "point_fp32" single precision) are
// carried in Geometry_flat_data::positions.
[[nodiscard]] auto is_structural_attribute(const std::string& element, const std::string& name) -> bool
{
    return (element == "vertex") && ((name == "point") || (name == "point_fp32"));
}

void dump_attributes(
    const std::string&       element,
    GEO::AttributesManager&  attributes_manager,
    Geometry_flat_data&      out
)
{
    GEO::vector<std::string> attribute_names;
    attributes_manager.list_attribute_names(attribute_names);
    for (GEO::index_t i = 0; i < attribute_names.size(); ++i) {
        const std::string& name = attribute_names[i];
        if (is_structural_attribute(element, name)) {
            continue;
        }
        GEO::AttributeStore* store = attributes_manager.find_attribute_store(name);
        if (store == nullptr) {
            continue;
        }
        if (
            !GEO::AttributeStore::element_typeid_name_is_known(store->element_typeid_name()) ||
            !GEO::AttributeStore::element_by_typeid_name_is_trivially_copyable(store->element_typeid_name())
        ) {
            log_geometry->warn("geometry_to_flat_data: skipping {} attribute '{}' (unsupported element type)", element, name);
            continue;
        }
        Geometry_attribute_record record;
        record.name         = name;
        record.element      = element;
        record.element_type = GEO::AttributeStore::element_type_name_by_element_typeid_name(store->user_element_typeid_name());
        record.element_size = store->element_size();
        record.dimension    = store->dimension();
        record.item_count   = attributes_manager.size();
        const std::size_t byte_count = record.item_count * record.dimension * record.element_size;
        const std::byte* data = static_cast<const std::byte*>(store->data());
        record.bytes.assign(data, data + byte_count);
        out.attributes.push_back(std::move(record));
    }
}

[[nodiscard]] auto restore_attributes(
    const std::string&        element,
    GEO::AttributesManager&   attributes_manager,
    const Geometry_flat_data& data
) -> bool
{
    for (const Geometry_attribute_record& record : data.attributes) {
        if (record.element != element) {
            continue;
        }
        if (!GEO::AttributeStore::element_type_name_is_known(record.element_type)) {
            log_geometry->warn("geometry_from_flat_data: skipping {} attribute '{}' (unknown element type '{}')", element, record.name, record.element_type);
            continue;
        }
        if (record.item_count != attributes_manager.size()) {
            log_geometry->warn(
                "geometry_from_flat_data: {} attribute '{}' item count {} does not match element count {}",
                element, record.name, record.item_count, attributes_manager.size()
            );
            return false;
        }
        GEO::AttributeStore* store = GEO::AttributeStore::create_attribute_store_by_element_type_name(
            record.element_type,
            static_cast<GEO::index_t>(record.dimension)
        );
        if (store == nullptr) {
            log_geometry->warn("geometry_from_flat_data: could not create store for {} attribute '{}'", element, record.name);
            continue;
        }
        attributes_manager.bind_attribute_store(record.name, store);
        const std::size_t byte_count = record.item_count * record.dimension * record.element_size;
        if ((record.bytes.size() != byte_count) || (store->element_size() != record.element_size)) {
            log_geometry->warn(
                "geometry_from_flat_data: {} attribute '{}' has inconsistent size (bytes {} expected {}, element size {} expected {})",
                element, record.name, record.bytes.size(), byte_count, store->element_size(), record.element_size
            );
            return false;
        }
        if (byte_count > 0) {
            std::memcpy(store->data(), record.bytes.data(), byte_count);
        }
    }
    return true;
}

} // anonymous namespace

void register_geogram_attribute_types()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    GEO::geo_register_attribute_type<GEO::vec2f>("vec2f");
    GEO::geo_register_attribute_type<GEO::vec3f>("vec3f");
    GEO::geo_register_attribute_type<GEO::vec4f>("vec4f");
    GEO::geo_register_attribute_type<GEO::vec2i>("vec2i");
    GEO::geo_register_attribute_type<GEO::vec3i>("vec3i");
    GEO::geo_register_attribute_type<GEO::vec4i>("vec4i");
    GEO::geo_register_attribute_type<GEO::vec2u>("vec2u");
    GEO::geo_register_attribute_type<GEO::vec3u>("vec3u");
    GEO::geo_register_attribute_type<GEO::vec4u>("vec4u");
}

auto geometry_to_flat_data(const Geometry& geometry) -> Geometry_flat_data
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    Geometry_flat_data data;
    data.vertex_count = mesh.vertices.nb();
    data.facet_count  = mesh.facets.nb();
    data.corner_count = mesh.facet_corners.nb();
    data.edge_count   = mesh.edges.nb();

    // Positions, single precision (erhe geometry uses single precision;
    // convert defensively when the mesh is double precision).
    data.positions.resize(data.vertex_count * 3);
    if (mesh.vertices.single_precision()) {
        for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
            const float* p = mesh.vertices.single_precision_point_ptr(v);
            data.positions[(v * 3) + 0] = p[0];
            data.positions[(v * 3) + 1] = p[1];
            data.positions[(v * 3) + 2] = p[2];
        }
    } else {
        for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
            const double* p = mesh.vertices.point_ptr(v);
            data.positions[(v * 3) + 0] = static_cast<float>(p[0]);
            data.positions[(v * 3) + 1] = static_cast<float>(p[1]);
            data.positions[(v * 3) + 2] = static_cast<float>(p[2]);
        }
    }

    // Rings + fan triangulation over geogram vertex ids.
    data.facet_vertex_counts .reserve(data.facet_count);
    data.facet_vertex_indices.reserve(data.corner_count);
    for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
        const GEO::index_t corner_count = mesh.facets.nb_vertices(f);
        data.facet_vertex_counts.push_back(corner_count);
        for (GEO::index_t lv = 0; lv < corner_count; ++lv) {
            data.facet_vertex_indices.push_back(mesh.facets.vertex(f, lv));
        }
        for (GEO::index_t lv = 1; (lv + 1) < corner_count; ++lv) {
            data.triangle_indices.push_back(mesh.facets.vertex(f, 0));
            data.triangle_indices.push_back(mesh.facets.vertex(f, lv));
            data.triangle_indices.push_back(mesh.facets.vertex(f, lv + 1));
        }
    }

    data.edge_vertex_indices.resize(data.edge_count * 2);
    for (GEO::index_t e = 0; e < mesh.edges.nb(); ++e) {
        data.edge_vertex_indices[(e * 2) + 0] = mesh.edges.vertex(e, 0);
        data.edge_vertex_indices[(e * 2) + 1] = mesh.edges.vertex(e, 1);
    }

    dump_attributes("vertex", mesh.vertices     .attributes(), data);
    dump_attributes("facet",  mesh.facets       .attributes(), data);
    dump_attributes("corner", mesh.facet_corners.attributes(), data);
    dump_attributes("edge",   mesh.edges        .attributes(), data);
    return data;
}

auto geometry_from_flat_data(const Geometry_flat_data& data, Geometry& geometry) -> bool
{
    // Structural consistency
    if (data.positions.size() != data.vertex_count * 3) {
        log_geometry->warn("geometry_from_flat_data: position count mismatch");
        return false;
    }
    if (data.facet_vertex_counts.size() != data.facet_count) {
        log_geometry->warn("geometry_from_flat_data: facet count mismatch");
        return false;
    }
    if (data.facet_vertex_indices.size() != data.corner_count) {
        log_geometry->warn("geometry_from_flat_data: corner count mismatch");
        return false;
    }
    if (data.edge_vertex_indices.size() != data.edge_count * 2) {
        log_geometry->warn("geometry_from_flat_data: edge count mismatch");
        return false;
    }
    std::size_t corner_sum = 0;
    for (const uint32_t corner_count : data.facet_vertex_counts) {
        if (corner_count < 3) {
            log_geometry->warn("geometry_from_flat_data: facet with fewer than 3 corners");
            return false;
        }
        corner_sum += corner_count;
    }
    if (corner_sum != data.corner_count) {
        log_geometry->warn("geometry_from_flat_data: facet corner sum does not match corner count");
        return false;
    }
    for (const uint32_t vertex_index : data.facet_vertex_indices) {
        if (vertex_index >= data.vertex_count) {
            log_geometry->warn("geometry_from_flat_data: corner vertex index out of range");
            return false;
        }
    }
    for (const uint32_t vertex_index : data.edge_vertex_indices) {
        if (vertex_index >= data.vertex_count) {
            log_geometry->warn("geometry_from_flat_data: edge vertex index out of range");
            return false;
        }
    }

    // The erhe Mesh_attributes wrapper holds GEO::Attribute bindings; drop
    // them while the stores are (re)created, mirroring the geofile load
    // path in scene_serialization.
    geometry.get_attributes().unbind();
    GEO::Mesh& mesh = geometry.get_mesh();
    mesh.clear(false, false);
    mesh.vertices.set_single_precision();

    mesh.vertices.create_vertices(static_cast<GEO::index_t>(data.vertex_count));
    for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
        float* p = mesh.vertices.single_precision_point_ptr(v);
        p[0] = data.positions[(v * 3) + 0];
        p[1] = data.positions[(v * 3) + 1];
        p[2] = data.positions[(v * 3) + 2];
    }

    std::size_t corner_offset = 0;
    for (std::size_t f = 0; f < data.facet_count; ++f) {
        const uint32_t     corner_count = data.facet_vertex_counts[f];
        const GEO::index_t facet        = mesh.facets.create_polygon(corner_count);
        for (uint32_t lv = 0; lv < corner_count; ++lv) {
            mesh.facets.set_vertex(facet, lv, data.facet_vertex_indices[corner_offset + lv]);
        }
        corner_offset += corner_count;
    }

    if (data.edge_count > 0) {
        mesh.edges.create_edges(static_cast<GEO::index_t>(data.edge_count));
        for (GEO::index_t e = 0; e < mesh.edges.nb(); ++e) {
            mesh.edges.set_vertex(e, 0, data.edge_vertex_indices[(e * 2) + 0]);
            mesh.edges.set_vertex(e, 1, data.edge_vertex_indices[(e * 2) + 1]);
        }
    }

    const bool attributes_ok =
        restore_attributes("vertex", mesh.vertices     .attributes(), data) &&
        restore_attributes("facet",  mesh.facets       .attributes(), data) &&
        restore_attributes("corner", mesh.facet_corners.attributes(), data) &&
        restore_attributes("edge",   mesh.edges        .attributes(), data);

    // Facet adjacency is structural / derived; reconnect deterministically.
    mesh.facets.connect();

    geometry.get_attributes().bind();
    return attributes_ok;
}

}
