#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace erhe::geometry {

class Geometry;

// Flat, glTF-accessor-shaped serialization of a Geometry
// (doc/gltf-scene-roundtrip-plan.md phase 2). Plain buffers that map 1:1
// onto glTF accessors / buffer views plus the ERHE_geometry attribute
// records; pure data with no glTF or GPU dependency, so the round-trip is
// unit-testable. The attribute dump carries raw geogram attribute store
// bytes (every attribute, including erhe's *_present flags), making the
// round-trip bit-exact - the same approach geogram's own geofile
// serialization uses.

// One geogram attribute, dumped raw. element identifies the attribute
// manager: "vertex", "facet", "corner" (facet corners, in the flat
// facet_vertex_indices order) or "edge".
class Geometry_attribute_record
{
public:
    std::string            name;
    std::string            element;
    std::string            element_type; // geogram element type name ("float", "vec3", "index_t", "bool", ...)
    std::size_t            element_size{0}; // bytes per scalar of the store
    std::size_t            dimension{0};    // scalars per item
    std::size_t            item_count{0};
    std::vector<std::byte> bytes;           // item_count * dimension * element_size raw store bytes
};

class Geometry_flat_data
{
public:
    std::size_t                            vertex_count{0};
    std::size_t                            facet_count{0};
    std::size_t                            corner_count{0};
    std::size_t                            edge_count{0};
    // Vertex positions, single precision (geogram "point_fp32"); also the
    // core glTF POSITION accessor. vertex_count * 3 floats.
    std::vector<float>                     positions;
    // Render triangles: each facet fan-triangulated over geogram vertex
    // ids (the same triangulation scheme the Primitive builder uses).
    std::vector<uint32_t>                  triangle_indices;
    // Polygon rings: one corner count per facet plus the flat
    // corner->vertex ids in facet-corner order (the same shape as
    // UsdGeomMesh faceVertexCounts / faceVertexIndices).
    std::vector<uint32_t>                  facet_vertex_counts;
    std::vector<uint32_t>                  facet_vertex_indices;
    // Edge endpoints (geogram edges are structural, not attributes):
    // edge_count * 2 vertex ids.
    std::vector<uint32_t>                  edge_vertex_indices;
    std::vector<Geometry_attribute_record> attributes;
};

// Registers erhe's geogram attribute element types (vec2f/vec3f/... used by
// Mesh_attributes) with geogram's attribute-type registry so attribute
// stores can be enumerated and re-created generically (geofile save/load,
// geometry_to_flat_data / geometry_from_flat_data). Idempotent; call once
// after GEO::initialize().
void register_geogram_attribute_types();

[[nodiscard]] auto geometry_to_flat_data(const Geometry& geometry) -> Geometry_flat_data;

// Rebuilds geometry (expected freshly constructed) from flat data:
// vertices / facets / edges recreated in order, attribute stores restored
// byte-exact, facet adjacency reconnected. Returns false (leaving geometry
// partially filled) when the data is structurally inconsistent.
[[nodiscard]] auto geometry_from_flat_data(const Geometry_flat_data& data, Geometry& geometry) -> bool;

}
