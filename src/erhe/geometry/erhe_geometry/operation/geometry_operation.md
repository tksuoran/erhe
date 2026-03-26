# Geometry_operation Base Class

## Purpose

`Geometry_operation` is the base class for all mesh topology operations
(Conway operators, subdivision, etc.). It provides infrastructure for:

1. **Creating destination vertices** from source vertices, face centroids,
   and edge midpoints
2. **Tracking provenance** - which source elements contributed to each
   destination element, with weights
3. **Interpolating mesh attributes** (positions, normals, texcoords,
   colors) from source to destination using the provenance weights
4. **Post-processing** - sanitize the mesh and rebuild connectivity,
   edges, normals, and texture coordinates

## Architecture

An operation subclass inherits from `Geometry_operation`, receives a
const source `Geometry` and a mutable destination `Geometry`, then
implements a `build()` method that:

1. Creates destination vertices using the provided helpers
2. Creates destination facets and sets their corner vertices
3. Calls `post_processing()` to interpolate attributes and finalize

The base class tracks five provenance tables, each a `Source_table`
instance (encapsulates `vector<vector<pair<float, index_t>>>` with
lazy resize and `add(dst, weight, src)` API):

| Table | Key | Value | Used for |
|-------|-----|-------|----------|
| `m_dst_vertex_sources` | dst vertex | (weight, src vertex) pairs | Vertex position interpolation |
| `m_dst_vertex_corner_sources` | dst vertex | (weight, src corner) pairs | Per-corner attribute inheritance |
| `m_dst_corner_sources` | dst corner | (weight, src corner) pairs | Corner attribute interpolation |
| `m_dst_facet_sources` | dst facet | (weight, src facet) pairs | Facet attribute interpolation |
| `m_dst_edge_sources` | dst edge | (weight, src edge) pairs | Edge attribute interpolation (unused) |

## Key Methods

### Vertex Creation

- **`make_dst_vertices_from_src_vertices()`** - copies all source
  vertices 1:1 to the destination with weight 1.0 each. Used by most
  operations that preserve original vertices (kis, join, gyro, meta,
  subdivide).

- **`make_facet_centroids()`** - creates one destination vertex per
  source face at the face centroid (equal weight from all corner
  vertices). Used by dual, kis, join, gyro, meta, subdivide.

- **`make_edge_midpoints({t0, t1, ...})`** - creates N destination
  vertices per source edge at the specified parametric positions.
  `t=0` is vertex A, `t=1` is vertex B. Weights are `t` for A and
  `(1-t)` for B. Used by ambo (t=0.5), truncate (t=1/3, 2/3), gyro
  (t=1/3, 2/3), meta (t=0.5), subdivide (t=0.5).

- **`get_src_edge_new_vertex(va, vb, slot)`** - retrieves the
  destination vertex for a previously created edge midpoint. Handles
  direction reversal: if `va > vb`, the slot index is mirrored so
  that slot 0 always returns the point closest to the first argument.

### Facet and Corner Creation

- **`make_new_dst_facet_from_src_facet(src_facet, corner_count)`** -
  creates a destination polygon and registers facet provenance. Used
  by gyro, meta, subdivide. Operations that skip this (ambo, dual,
  kis, join, truncate) lose facet attribute interpolation.

- **`make_new_dst_corner_from_src_corner(dst_facet, local_corner, src_corner)`**
  - sets a corner's vertex from the 1:1 vertex copy and registers
  corner provenance. Used when the destination corner corresponds
  directly to a source corner.

- **`make_new_dst_corner_from_src_facet_centroid(dst_facet, local_corner, src_facet)`**
  - sets a corner's vertex to the previously created centroid vertex
  and distributes corner sources from the centroid's vertex sources.

- **`make_new_dst_corner_from_dst_vertex(dst_facet, local_corner, dst_vertex)`**
  - sets a corner's vertex to an existing destination vertex (e.g.,
  an edge midpoint) and distributes corner sources.

### Attribute Interpolation

**`interpolate_mesh_attributes()`** computes destination vertex
positions as weighted averages of source vertex positions:

```
dst_pos = Σ(weight_i * src_pos_i) / Σ(weight_i)
```

It also interpolates all typed attributes (normals, texcoords, colors,
tangents, joint weights, aniso control) using the corresponding source
tables. Facet attributes use `m_dst_facet_sources`, corner attributes
use `m_dst_corner_sources`.

**`post_processing()`** calls `interpolate_mesh_attributes()`, then
`sanitize()` (removes degenerate facets), then `process()` with flags
for connectivity, edges, centroids, smooth normals, and texture
coordinates.

## Usage Examples

### Dual (simplest operation)

```cpp
class Dual : public Geometry_operation {
    void build() {
        // Phase 1: create centroid vertices (one per face)
        make_facet_centroids();

        // Phase 2: for each source vertex, create a face connecting
        // the centroids of all incident faces
        for (GEO::index_t src_vertex : source_mesh.vertices) {
            const auto& src_corners = source.get_vertex_corners(src_vertex);
            GEO::index_t new_facet = destination_mesh.facets.create_polygon(src_corners.size());
            for (GEO::index_t i = 0; i < src_corners.size(); ++i) {
                make_new_dst_corner_from_src_facet_centroid(new_facet, i,
                    source.get_corner_facet(src_corners[i]));
            }
        }

        post_processing();
    }
};
```

### Subdivide (uses all three vertex types)

```cpp
class Subdivide : public Geometry_operation {
    void build() {
        // Create three types of destination vertices
        make_dst_vertices_from_src_vertices(); // original vertices
        make_facet_centroids();                // face centers
        make_edge_midpoints({0.5f});           // edge midpoints

        // For each face corner, create a quad:
        // [prev_edge_mid, original_vertex, next_edge_mid, face_centroid]
        for (GEO::index_t src_facet : source_mesh.facets) {
            for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
                GEO::index_t new_facet = make_new_dst_facet_from_src_facet(src_facet, 4);
                make_new_dst_corner_from_dst_vertex(new_facet, 0, prev_midpoint);
                make_new_dst_corner_from_src_corner(new_facet, 1, src_corner);
                make_new_dst_corner_from_dst_vertex(new_facet, 2, next_midpoint);
                make_new_dst_corner_from_src_facet_centroid(new_facet, 3, src_facet);
            }
        }

        post_processing();
    }
};
```

### Chamfer3 (bypasses base class vertex creation)

Chamfer3 does not use the base class vertex/facet helpers for its
main geometry. It creates vertices directly via
`destination_mesh.vertices.create_vertex()` and sets positions with
`set_pointf()`. It only uses the base class for `process()` in the
final step. This is because chamfer computes vertex positions through
its own LS fitting algorithm rather than weighted interpolation.

## Implementation Notes

- Provenance tracking uses `Source_table`, a class that encapsulates
  a `vector<vector<pair<float, index_t>>>` with lazy 1.75x growth.
  The `add(dst, weight, src)` method handles resize automatically.
  All five `add_*_source` methods delegate to `Source_table::add()`.
- Edge midpoints are keyed by ordered vertex pairs (min, max) in
  `m_src_edge_to_dst_vertex`. The `get_src_edge_new_vertex` method
  handles direction reversal by mirroring the slot index.
- `add_edge_source` exists but is unused - edge attribute
  interpolation is not implemented (TODO in the code).
- The `rhs`/`rhs_mesh` members support binary operations (CSG) where
  two source geometries are combined into one destination. For unary
  operations, `rhs` is nullptr. The `source`/`source_mesh` members
  refer to the primary (or only) input geometry.
