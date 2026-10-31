#pragma once

#include "erhe_geometry/geometry.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/vertex_attribute_info.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

namespace erhe::primitive {

class Index_range;
class Material;
class Primitive_render_shape;

class Vertex_attributes
{
public:
    Vertex_attribute_info              position          ;
    Vertex_attribute_info              normal            ;
    Vertex_attribute_info              normal_smooth     ; // Editor wireframe bias requires smooth normal
    Vertex_attribute_info              tangent           ;
    Vertex_attribute_info              bitangent         ;
    Vertex_attribute_info              aniso_control     ;
    Vertex_attribute_info              id_vec4           ;
    Vertex_attribute_info              valency_edge_count;
    std::vector<Vertex_attribute_info> color             ;
    std::vector<Vertex_attribute_info> texcoord          ;
    std::vector<Vertex_attribute_info> joint_indices     ;
    std::vector<Vertex_attribute_info> joint_weights     ;
};

class Element_mappings;

class Build_context_root
{
public:
    Build_context_root(
        Buffer_mesh&      buffer_mesh,
        const GEO::Mesh&  mesh,
        const Build_info& build_info,
        Element_mappings& element_mappings
    );

    void get_mesh_info                  ();
    void get_vertex_attributes          ();
    void calculate_bounding_volume      ();
    void calculate_joint_bounding_volumes(erhe::geometry::Mesh_attributes& mesh_attributes);
    // One atomic multi-pool allocation transaction for the whole mesh, run
    // AFTER the build so the meshopt passes (phase 5b) can change the vertex
    // and index counts the allocation is sized for. Everything the build needs
    // before then - the per-type index sub-ranges, the staging sizes - comes
    // from mesh counts, not from allocated ranges.
    void allocate_buffers               ();
    void allocate_vertex_buffers        ();
    void allocate_edge_line_vertex_buffer();
    void allocate_edge_line_joint_buffer ();
    void allocate_expanded_fill_buffers  ();
    void allocate_index_buffer          ();
    void allocate_index_range           (Primitive_type primitive_type, std::size_t index_count, Index_range& out_range);

    Buffer_mesh&                           buffer_mesh;
    const GEO::Mesh&                       mesh;
    const Build_info&                      build_info;
    Element_mappings&                      element_mappings;
    std::size_t                            next_index_range_start{0};
    Vertex_attributes                      vertex_attributes;
    erhe::geometry::Mesh_info              mesh_info;
    const erhe::dataformat::Vertex_format& vertex_format;
    // Vertex position encoding of the SINK format, and the pack that goes with it:
    // encoded = clamp((p - position_encode_center) * position_encode_inv_scale, -1, 1).
    // Computed right after calculate_bounding_volume() - which the constructor runs
    // before any position is written - so the AABB is already known when the first
    // vertex arrives. Passthrough leaves the two vectors unused.
    erhe::dataformat::Vertex_position_encoding position_encoding{erhe::dataformat::Vertex_position_encoding::passthrough};
    GEO::vec3f                             position_encode_inv_scale{1.0f, 1.0f, 1.0f};
    GEO::vec3f                             position_encode_center   {0.0f, 0.0f, 0.0f};

    std::size_t                            total_vertex_count{0};
    std::size_t                            total_index_count {0};
    bool                                   build_failed{false};
};

class Build_context
{
public:
    Build_context(
        Buffer_mesh&      buffer_mesh,
        const GEO::Mesh&  mesh,
        const Build_info& build_info,
        Element_mappings& element_mappings,
        Normal_style      normal_style
    );
    ~Build_context() noexcept;

    // True when the mesh has nothing to build for the requested primitive
    // types: no vertices and no indices. Such a build succeeds with an empty
    // Buffer_mesh (see Primitive_builder::build()).
    auto is_empty() const -> bool;
    auto is_ready() const -> bool;

    // What the optimized variant is built from: the CORNER-VERTEX PREFIX of
    // every sink stream (the centroid-point vertices this build appends after
    // the corners are not part of the fill mesh) plus the fill triangle
    // indices, decoded to 32 bits. The per-corner facet id bytes are zeroed on
    // the way out - welding makes them meaningless, and left in place the
    // bitwise compare would see them differ at every facet boundary and merge
    // nothing at all. Nothing reads them from the variant: ID rendering pins
    // itself to the original build.
    //
    // Must be called BEFORE allocate_and_bind_writers(), which hands the staged
    // bytes to the sink. Returns false - having touched neither output - when
    // there is nothing to optimize.
    //
    // `out_lod_vertices`, when non-null, also receives the LOD generation
    // inputs of the same corner prefix (extract_lod_vertex_data(), float data
    // read from the staged source-format bytes, seam attributes as the
    // optimized format carries them).
    auto take_optimizable_snapshot(
        std::vector<Mesh_optimize_stream>& out_streams,
        std::vector<uint32_t>&             out_fill_indices,
        Lod_vertex_data*                   out_lod_vertices
    ) -> bool;

    // Runs root.allocate_buffers() and hands every writer its destination.
    // Returns false when the build or the allocation failed, in which case no
    // writer has a range and none of them will flush.
    auto allocate_and_bind_writers() -> bool;

    void build_polygon_fill         ();
    void build_expanded_polygon_fill();
    void build_edge_lines           ();
    void build_centroid_points      ();

    Build_context_root root;

private:
    void build_polygon_id        ();

    [[nodiscard]] auto get_facet_normal() -> GEO::vec3f;

    void build_tangent_frame();

    // Single funnel for every position write: applies the sink format's encoding
    // (a no-op for passthrough) and forwards to the position attribute writer.
    void write_position(const Vertex_attribute_info& info, GEO::vec3f position);

    void build_vertex_position     ();
    void build_vertex_normal       (bool normal, bool smooth_normal);
    void build_vertex_tangent      ();
    void build_vertex_bitangent    ();
    void build_vertex_texcoord     (size_t usage_index);
    void build_vertex_color        (size_t usage_index);
    void build_vertex_aniso_control();
    [[nodiscard]] auto get_aniso_control() const -> GEO::vec2f;
    void build_vertex_joint_indices(size_t usage_index);
    void build_vertex_joint_weights(size_t usage_index);

    void build_centroid_position   ();
    void build_centroid_normal     ();
    void build_valency_edge_count  ();

    void build_corner_point_index  ();
    void build_triangle_fill_index ();

    GEO::vec3f v_position {};
    GEO::vec3f v_normal   {};
    GEO::vec4f v_tangent  {};
    GEO::vec3f v_bitangent{};

    GEO::index_t        mesh_facet {0};
    GEO::index_t        mesh_vertex{0};
    GEO::index_t        mesh_corner{0};
    uint32_t            vertex_buffer_index{0}; // primitive vertex index    .
    uint32_t            first_index        {0}; // primitive first index      . These make triangle primitive
    uint32_t            previous_index     {0}; // primitive previous index  .
    uint32_t            primitive_index    {0}; // triangle (TODO quad) index
    Normal_style        normal_style       {Normal_style::none};
    Index_buffer_writer index_writer;
    erhe::geometry::Mesh_attributes mesh_attributes;
    std::vector<std::unique_ptr<Vertex_buffer_writer>> vertex_writers;
    // Expanded solid-wireframe writers. These live here rather than inside
    // build_expanded_polygon_fill() because a writer must outlive the build:
    // it flushes in its destructor, and the destination range does not exist
    // until allocate_and_bind_writers() runs after the build.
    std::vector<std::unique_ptr<Vertex_buffer_writer>> expanded_vertex_writers;

private:
    // Edge-line side buffers. build_edge_lines() stages raw bytes here rather
    // than through a Vertex_buffer_writer, and allocate_and_bind_writers()
    // enqueues them once their ranges exist.
    std::vector<uint8_t> m_edge_line_vertex_data;
    std::vector<uint8_t> m_edge_line_joint_data;
    std::size_t          m_edge_line_vertex_bytes_written{0};
    std::size_t          m_edge_line_joint_bytes_written {0};

public:
    // Use root.element_mappings.corner_to_vertex_id
    // std::vector<size_t>  corner_indices;

    bool used_fallback_smooth_normal{false};
    bool used_fallback_tangent      {false};
    bool used_fallback_bitangent    {false};
    bool used_fallback_texcoord     {false};

    [[nodiscard]] auto get_attribute_writer(erhe::dataformat::Vertex_attribute_usage usage, std::size_t index = 0) -> Vertex_buffer_writer*;

    class Vertex_writers
    {
    public:
        Vertex_buffer_writer* position;
        Vertex_buffer_writer* normal;
        Vertex_buffer_writer* normal_smooth;
        Vertex_buffer_writer* tangent;
        Vertex_buffer_writer* bitangent;
        Vertex_buffer_writer* color_0;
        Vertex_buffer_writer* texcoord_0;
        Vertex_buffer_writer* joint_indices_0;
        Vertex_buffer_writer* joint_weights_0;
        Vertex_buffer_writer* id;
        Vertex_buffer_writer* aniso_control;
        Vertex_buffer_writer* valency_edge_count;

    };
    Vertex_writers attribute_writers;
};

class Primitive_builder final
{
public:
    Primitive_builder(
        Buffer_mesh&      buffer_mesh,
        const GEO::Mesh&  mesh,
        const Build_info& build_info,
        Element_mappings& element_mappings,
        Normal_style      normal_style,
        std::string_view  name,
        // Whether to build the optimized variant at all. Buffer_info::optimize_meshes
        // says the SESSION wants optimized meshes; this says THIS caller has
        // somewhere to put one. A builder invoked without it would run the whole
        // meshopt pass and allocate a second index range and vertex range per
        // stream for a shape that is discarded the moment build() returns.
        bool              build_optimized_variant
    );

    auto build() -> bool;

    // The meshoptimizer variant of what build() just built, or null when
    // optimization was not asked for or the optimizer refused the mesh. Valid
    // only after build() returned true.
    //
    // Handed back rather than attached: a Primitive_builder is given a
    // Buffer_mesh, not the Primitive that owns the variant slot, and the
    // variant is only ever published once it is complete.
    [[nodiscard]] auto take_optimized_render_shape() -> std::shared_ptr<Primitive_render_shape>;

private:
    Buffer_mesh&                            m_buffer_mesh;
    const GEO::Mesh&                        m_mesh;
    const Build_info&                       m_build_info;
    Element_mappings&                       m_element_mappings;
    const Normal_style                      m_normal_style;
    const std::string_view                  m_name;
    const bool                              m_build_optimized_variant;
    std::shared_ptr<Primitive_render_shape> m_optimized_render_shape;
};

// `out_optimized_shape`, when non-null, receives the optimized variant of this
// build - see Primitive_builder::take_optimized_render_shape(). It is left
// null when Buffer_info::optimize_meshes is off or the optimizer refused the
// mesh; the build itself succeeds either way. `name` only appears in the
// optimization log line. A mesh with nothing to draw (no facets) builds
// successfully to an empty Buffer_mesh with no ranges and no allocations.
auto build_buffer_mesh(
    Buffer_mesh&                             buffer_mesh,
    const GEO::Mesh&                         source_mesh,
    const Build_info&                        build_info,
    Element_mappings&                        element_mappings,
    Normal_style                             normal_style        = Normal_style::corner_normals,
    std::shared_ptr<Primitive_render_shape>* out_optimized_shape = nullptr,
    std::string_view                         name                = {}
) -> bool;

// The skinning weights the build writes for one vertex's WEIGHTS_0 (what the
// shader reads; doc/erhe/primitive.md "Skinning weights"): negative and
// non-finite weights become 0, the rest are divided by their sum, and a
// vertex whose sum is then 0 gets weight 1 on its first influence (slot 0,
// whatever joint JOINTS_0 names there). The result sums to one, so a skinned
// position is a convex combination of its joints' images.
[[nodiscard]] auto normalize_joint_weights(const GEO::vec4f& weights) -> GEO::vec4f;

// The four unorm8 values of normalized weights (normalize_joint_weights()),
// rounded so that they sum to exactly 255 (largest remainder): the shader's
// decode then sums to one up to float rounding, and a zero weight stays zero.
[[nodiscard]] auto quantize_joint_weights_unorm8(const GEO::vec4f& normalized_weights) -> std::array<uint8_t, 4>;

// Adds a vertex at its bind-space position to buffer_mesh.joint_bounding_boxes
// the way the build skins it: the joints that the built JOINTS_0 / WEIGHTS_0
// give a positive weight (a vertex without values is built as joint 0 at
// weight one, weights normalized by normalize_joint_weights()) and, as
// before, the joints with a positive JOINTS_1 / WEIGHTS_1 weight (which the
// shader does not read; other consumers of the boxes use them). Used by the
// build and by in-place vertex edits of a built mesh (the editor's vertex
// drag).
void include_vertex_in_joint_bounding_boxes(
    Buffer_mesh&                     buffer_mesh,
    erhe::geometry::Mesh_attributes& mesh_attributes,
    GEO::index_t                     vertex,
    const glm::vec3&                 position
);

} // namespace erhe::primitive
