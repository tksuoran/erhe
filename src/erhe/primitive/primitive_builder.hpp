#pragma once

#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/property_map.hpp"
#include "erhe/geometry/property_map_collection.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/primitive/buffer_info.hpp"
#include "erhe/primitive/buffer_writer.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/format_info.hpp"
#include "erhe/primitive/geometry_uploader.hpp"
#include "erhe/primitive/index_range.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/property_maps.hpp"
#include "erhe/primitive/vertex_attribute_info.hpp"
#include "erhe/gl/strong_gl_enums.hpp"

#include "Tracy.hpp"
#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>

namespace erhe::primitive
{

class Geometry_uploader;
class Index_range;
class Material;

class Vertex_attributes
{
public:
    Vertex_attribute_info position         ;
    Vertex_attribute_info normal           ;
    Vertex_attribute_info normal_flat      ;
    Vertex_attribute_info normal_smooth    ;
    Vertex_attribute_info tangent          ;
    Vertex_attribute_info bitangent        ;
    Vertex_attribute_info color            ;
    Vertex_attribute_info texcoord         ;
    Vertex_attribute_info id_vec3          ;
    Vertex_attribute_info attribute_id_uint;
};

class Build_context_root
{
public:
    Build_context_root(const erhe::geometry::Geometry& geometry,
                       const Geometry_uploader&        geometry_uploader,
                       Primitive_geometry*             primitive_geometry);

    void get_mesh_info();
    void get_vertex_attributes();
    void calculate_bounding_box(erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>* point_locations);
    void allocate_vertex_buffer();
    void allocate_index_buffer ();
    void allocate_index_range(gl::Primitive_type primitive_type,
                              size_t             index_count,
                              Index_range&       range);

    const erhe::geometry::Geometry& geometry;
    const Geometry_uploader&        geometry_uploader;
    Primitive_geometry*             primitive_geometry{nullptr};
    size_t                          next_index_range_start{0};
    Vertex_attributes               attributes;
    erhe::geometry::Mesh_info       mesh_info;
    erhe::graphics::Vertex_format*  vertex_format     {nullptr};
    size_t                          vertex_stride     {0};
    size_t                          total_vertex_count{0};
    size_t                          total_index_count {0};
};

class Build_context
{
public:
    Build_context(const erhe::geometry::Geometry& geometry,
                  const Geometry_uploader&        geometry_uploader,
                  const Normal_style              normal_style,
                  Primitive_geometry*             primitive_geometry);
    ~Build_context();

    void build_polygon_fill      ();
    void build_edge_lines        ();
    void build_centroid_points   ();

private:
    void build_polygon_id        ();
    auto get_polygon_normal() -> glm::vec3;

    void build_vertex_position   ();
    void build_vertex_normal     ();
    void build_vertex_tangent    ();
    void build_vertex_bitangent  ();
    void build_vertex_texcoord   ();
    void build_vertex_color      ();

    void build_centroid_position ();
    void build_centroid_normal   ();

    void build_corner_point_index ();
    void build_triangle_fill_index();

    Build_context_root                root;
    erhe::geometry::Polygon_id        polygon_id       {0};
    erhe::geometry::Polygon_corner_id polygon_corner_id{0};
    erhe::geometry::Point_id          point_id         {0};
    erhe::geometry::Corner_id         corner_id        {0};
    uint32_t                          vertex_index     {0};
    uint32_t                          first_index      {0};
    uint32_t                          previous_index   {0};
    uint32_t                          polygon_index    {0};
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
    // Controls what kind of mesh should be built from geometry

    Primitive_builder() = delete;

    Primitive_builder(const erhe::geometry::Geometry& geometry,
                      const Geometry_uploader&        geometry_uploader,
                      const Normal_style              normal_style);

    ~Primitive_builder();

    auto build() -> Primitive_geometry;

    void build(Primitive_geometry* primitive_geometry);

    static void prepare_vertex_format(const Format_info& format_info,
                                      Buffer_info&       buffer_info);

private:
    const erhe::geometry::Geometry& m_geometry;
    const Geometry_uploader&        m_geometry_uploader;
    //Format_info                     m_format_info;
    Normal_style                    m_normal_style;
};

auto make_primitive(const erhe::geometry::Geometry& geometry,
                    const Geometry_uploader&        geometry_uploader,
                    const Normal_style              normal_style = Normal_style::corner_normals)
-> Primitive_geometry;

auto make_primitive_shared(const erhe::geometry::Geometry& geometry,
                           const Geometry_uploader&        geometry_uploader,
                           const Normal_style              normal_style = Normal_style::corner_normals)
-> std::shared_ptr<Primitive_geometry>;

} // namespace erhe::primitive
