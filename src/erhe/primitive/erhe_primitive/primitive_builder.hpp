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
    Vertex_attribute_info position     ;
    Vertex_attribute_info normal       ;
    //Vertex_attribute_info normal_flat  ;
    Vertex_attribute_info normal_smooth; // Editor wireframe bias requires smooth normal
    Vertex_attribute_info tangent      ;
    Vertex_attribute_info bitangent    ;
    Vertex_attribute_info color        ;
    Vertex_attribute_info aniso_control;
    Vertex_attribute_info texcoord     ;
    Vertex_attribute_info id_vec3      ;
    //// TODO Vertex_attribute_info attribute_id_uint;
    Vertex_attribute_info joint_indices;
    Vertex_attribute_info joint_weights;
    Vertex_attribute_info valency_edge_count;
};

class Element_mappings;

class Build_context_root
{
public:
    Build_context_root(
        const erhe::geometry::Geometry& geometry,
        const Build_info&               build_info,
        Element_mappings&               element_mappings,
        Buffer_mesh*                    buffer_mesh
    );

    void get_mesh_info            ();
    void get_vertex_attributes    ();
    void calculate_bounding_volume(erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>* point_locations);
    void allocate_vertex_buffer   ();
    void allocate_index_buffer    ();
    void allocate_index_range     (const gl::Primitive_type primitive_type, const std::size_t index_count, Index_range& out_range);

    const erhe::geometry::Geometry&      geometry;
    const Build_info&                    build_info;
    Element_mappings&                    element_mappings;
    Buffer_mesh*                         buffer_mesh{nullptr};
    std::size_t                          next_index_range_start{0};
    Vertex_attributes                    attributes;
    erhe::geometry::Mesh_info            mesh_info;
    const erhe::graphics::Vertex_format& vertex_format;
    std::size_t                          vertex_stride     {0};
    std::size_t                          total_vertex_count{0};
    std::size_t                          total_index_count {0};
};

class Build_context
{
public:
    Build_context(
        const erhe::geometry::Geometry& geometry,
        const Build_info&               build_info,
        Element_mappings&               element_mappings,
        const Normal_style              normal_style,
        Buffer_mesh*                    buffer_mesh
    );
    ~Build_context() noexcept;

    void build_polygon_fill   ();
    void build_edge_lines     ();
    void build_centroid_points();

    Build_context_root root;

private:
    void build_polygon_id        ();

    [[nodiscard]] auto get_polygon_normal() -> glm::vec3;

    void build_vertex_position     ();
    void build_vertex_normal       (bool normal, bool smooth_normal);
    void build_vertex_tangent      ();
    void build_vertex_bitangent    ();
    void build_vertex_texcoord     ();
    void build_vertex_color        (const uint32_t polygon_corner_count);
    void build_vertex_aniso_control();
    void build_vertex_joint_indices();
    void build_vertex_joint_weights();

    void build_centroid_position   ();
    void build_centroid_normal     ();
    void build_valency_edge_count  ();

    void build_corner_point_index  ();
    void build_triangle_fill_index ();

    erhe::geometry::Polygon_id        polygon_id       {0};
    erhe::geometry::Polygon_corner_id polygon_corner_id{0};
    erhe::geometry::Point_id          point_id         {0};
    erhe::geometry::Corner_id         corner_id        {0};
    uint32_t                          vertex_index     {0}; // primitive vertex index    .
    uint32_t                          first_index      {0}; // primitive first index      . These make triangle primitive
    uint32_t                          previous_index   {0}; // primitive previous index  .
    uint32_t                          polygon_index    {0};
    uint32_t                          primitive_index  {0}; // triangle (TODO quad) index
    Normal_style                      normal_style     {Normal_style::none};
    Vertex_buffer_writer              vertex_writer;
    Index_buffer_writer               index_writer;
    Property_maps                     property_maps;

    bool used_fallback_smooth_normal{false};
    bool used_fallback_tangent      {false};
    bool used_fallback_bitangent    {false};
    bool used_fallback_texcoord     {false};
};

class Primitive_builder final
{
public:
    Primitive_builder(
        const erhe::geometry::Geometry& geometry,
        const Build_info&               build_info,
        Element_mappings&               element_mappings,
        Normal_style                    normal_style
    );

    [[nodiscard]] auto build() -> Buffer_mesh;

    void build(Buffer_mesh* buffer_mesh);

private:
    const erhe::geometry::Geometry& m_geometry;
    const Build_info&               m_build_info;
    Element_mappings&               m_element_mappings;
    const Normal_style              m_normal_style;
};

[[nodiscard]] auto make_buffer_mesh(
    const erhe::geometry::Geometry& geometry,
    const Build_info&               build_info,
    Element_mappings&               element_mappings,
    Normal_style                    normal_style = Normal_style::corner_normals
) -> Buffer_mesh;

} // namespace erhe::primitive
