#pragma once

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/property_map.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/index_range.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/property_maps.hpp"
#include "erhe_primitive/vertex_attribute_info.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>

namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
}

namespace erhe::primitive {

class Index_range;
class Material;

class Vertex_attributes
{
public:
    Vertex_attribute_info              position          ;
    Vertex_attribute_info              normal            ;
    Vertex_attribute_info              normal_smooth     ; // Editor wireframe bias requires smooth normal
    Vertex_attribute_info              tangent           ;
    Vertex_attribute_info              bitangent         ;
    Vertex_attribute_info              aniso_control     ;
    Vertex_attribute_info              id_vec3           ;
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

    void get_mesh_info            ();
    void get_vertex_attributes    ();
    void calculate_bounding_volume();
    void allocate_vertex_buffer   ();
    void allocate_index_buffer    ();
    void allocate_index_range     (const gl::Primitive_type primitive_type, const std::size_t index_count, Index_range& out_range);

    Buffer_mesh&                         buffer_mesh;
    const GEO::Mesh&                     mesh;
    const Build_info&                    build_info;
    Element_mappings&                    element_mappings;
    std::size_t                          next_index_range_start{0};
    Vertex_attributes                    vertex_attributes;
    Mesh_info                            mesh_info;
    const erhe::graphics::Vertex_format& vertex_format;
    std::size_t                          vertex_stride     {0};
    std::size_t                          total_vertex_count{0};
    std::size_t                          total_index_count {0};
};

class Build_context
{
public:
    Build_context(
        Buffer_mesh&       buffer_mesh,
        const GEO::Mesh&   mesh,
        const Build_info&  build_info,
        Element_mappings&  element_mappings,
        const Normal_style normal_style
    );
    ~Build_context() noexcept;

    auto is_ready() const -> bool;

    void build_polygon_fill   ();
    void build_edge_lines     ();
    void build_centroid_points();

    Build_context_root root;

private:
    void build_polygon_id        ();

    [[nodiscard]] auto get_facet_normal() -> GEO::vec3f;

    void build_tangent_frame();

    void build_vertex_position     ();
    void build_vertex_normal       (bool normal, bool smooth_normal);
    void build_vertex_tangent      ();
    void build_vertex_bitangent    ();
    void build_vertex_texcoord     (size_t usage_index);
    void build_vertex_color        (size_t usage_index);
    void build_vertex_aniso_control();
    void build_vertex_joint_indices(size_t usage_index);
    void build_vertex_joint_weights(size_t usage_index);

    void build_centroid_position   ();
    void build_centroid_normal     ();
    void build_valency_edge_count  ();

    void build_corner_point_index  ();
    void build_triangle_fill_index ();

    GEO::vec3  v_position {};
    GEO::vec3f v_normal   {};
    GEO::vec4f v_tangent  {};
    GEO::vec3f v_bitangent{};

    GEO::index_t         mesh_facet {0};
    GEO::index_t         mesh_vertex{0};
    GEO::index_t         mesh_corner{0};
    uint32_t             vertex_buffer_index{0}; // primitive vertex index    .
    uint32_t             first_index        {0}; // primitive first index      . These make triangle primitive
    uint32_t             previous_index     {0}; // primitive previous index  .
    uint32_t             primitive_index    {0}; // triangle (TODO quad) index
    Normal_style         normal_style       {Normal_style::none};
    Vertex_buffer_writer vertex_writer;
    Index_buffer_writer  index_writer;
    Mesh_attributes      mesh_attributes;
    // Use root.element_mappings.corner_to_vertex_id
    // std::vector<size_t>  corner_indices;

    bool used_fallback_smooth_normal{false};
    bool used_fallback_tangent      {false};
    bool used_fallback_bitangent    {false};
    bool used_fallback_texcoord     {false};
};

class Primitive_builder final
{
public:
    Primitive_builder(
        Buffer_mesh&      buffer_mesh,
        const GEO::Mesh&  mesh,
        const Build_info& build_info,
        Element_mappings& element_mappings,
        Normal_style      normal_style
    );

    auto build() -> bool;

private:
    Buffer_mesh&       m_buffer_mesh;
    const GEO::Mesh&   m_mesh;
    const Build_info&  m_build_info;
    Element_mappings&  m_element_mappings;
    const Normal_style m_normal_style;
};

auto make_buffer_mesh(
    Buffer_mesh&       buffer_mesh,
    const GEO::Mesh&   source_mesh,
    const Build_info&  build_info,
    Element_mappings&  element_mappings,
    Normal_style       normal_style = Normal_style::corner_normals
) -> bool;

} // namespace erhe::primitive
