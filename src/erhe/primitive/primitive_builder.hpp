#pragma once

#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/property_map.hpp"
#include "erhe/geometry/property_map_collection.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/primitive/buffer_info.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/format_info.hpp"
#include "erhe/primitive/index_range.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/gl/strong_gl_enums.hpp"

#include "Tracy.hpp"
#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::primitive
{

struct Index_range;
struct Material;

struct Primitive_build_context final
{
    Primitive_build_context(erhe::graphics::Buffer_transfer_queue& queue,
                            const Format_info&                     format_info,
                            const Buffer_info&                     buffer_info);
    ~Primitive_build_context();
    Primitive_build_context(const Primitive_build_context& other);
    void operator=         (Primitive_build_context&)  = delete;
    Primitive_build_context(Primitive_build_context&& other) noexcept;
    void operator=         (Primitive_build_context&&) = delete;

    erhe::graphics::Buffer_transfer_queue& queue;
    Format_info                            format_info;
    Buffer_info                            buffer_info;
};

struct Vertex_attribute_info
{
    Vertex_attribute_info();
    Vertex_attribute_info(erhe::graphics::Vertex_format*               vertex_format,
                          gl::Vertex_attrib_type                       default_data_type,
                          size_t                                       dimension,
                          erhe::graphics::Vertex_attribute::Usage_type semantic,
                          unsigned int                                 semantic_index);

    auto is_valid() -> bool;

    const erhe::graphics::Vertex_attribute* attribute{nullptr};
    gl::Vertex_attrib_type                  data_type{gl::Vertex_attrib_type::float_};
    size_t                                  offset{std::numeric_limits<size_t>::max()};
    size_t                                  size{0};
};

struct Vertex_buffer_writer
{
    explicit Vertex_buffer_writer(Primitive_geometry&            primitive_geometry,
                                  const Primitive_build_context& context);
    virtual ~Vertex_buffer_writer();

    void write(Vertex_attribute_info& attribute, glm::vec2 value);
    void write(Vertex_attribute_info& attribute, glm::vec3 value);
    void write(Vertex_attribute_info& attribute, glm::vec4 value);
    void write(Vertex_attribute_info& attribute, uint32_t value);
    void move (size_t relative_offset);

    Primitive_geometry&            primitive_geometry;
    const Primitive_build_context& context;
    std::vector<std::uint8_t>      vertex_data;
    gsl::span<std::uint8_t>        vertex_data_span;
    size_t                         vertex_write_offset{0};
};

struct Index_buffer_writer
{
    Index_buffer_writer(Primitive_geometry&            primitive_geometry,
                        const Primitive_build_context& context,
                        erhe::geometry::Mesh_info&     mesh_info);
    virtual ~Index_buffer_writer();

    void write_corner  (uint32_t v0);
    void write_triangle(uint32_t v0, uint32_t v1, uint32_t v2);
    void write_edge    (uint32_t v0, uint32_t v1);
    void write_centroid(uint32_t v0);

    Primitive_geometry&            primitive_geometry;
    const Primitive_build_context& context;
    const gl::Draw_elements_type   index_type;
    const size_t                   index_type_size{0};
    std::vector<std::uint8_t>      index_data;
    gsl::span<std::uint8_t>        index_data_span;
    gsl::span<std::uint8_t>        corner_point_index_data_span;
    gsl::span<std::uint8_t>        fill_index_data_span;
    gsl::span<std::uint8_t>        edge_line_index_data_span;
    gsl::span<std::uint8_t>        polygon_centroid_index_data_span;

    size_t corner_point_indices_written    {0};
    size_t triangle_indices_written        {0};
    size_t edge_line_indices_written       {0};
    size_t polygon_centroid_indices_written{0};
};

class Primitive_builder final
{
public:
    // Controls what kind of mesh should be built from geometry

    struct Property_maps
    {
        Property_maps(const erhe::geometry::Geometry& geometry,
                      const Format_info&              format_info);

        template <typename Key_type, typename Value_type>
        auto
        find_or_create(const erhe::geometry::Property_map_collection<Key_type>&  geometry_attributes,
                       erhe::geometry::Property_map_collection<Key_type>&        attributes,
                       const std::string&                                        name)
        -> erhe::geometry::Property_map<Key_type, Value_type> *
        {
            ZoneScoped;

            auto map = geometry_attributes.template find<Value_type>(name);
            if (map == nullptr)
            {
                map = attributes.template create<Value_type>(name);
            }
            return map;
        }

        erhe::geometry::Property_map_collection<erhe::geometry::Point_id>   point_attributes;
        erhe::geometry::Property_map_collection<erhe::geometry::Corner_id>  corner_attributes;
        erhe::geometry::Property_map_collection<erhe::geometry::Polygon_id> polygon_attributes;
        erhe::geometry::Property_map_collection<erhe::geometry::Edge_id>    edge_attributes;

        erhe::geometry::Property_map<erhe::geometry::Polygon_id, glm::vec3>* polygon_ids_vector3 {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Polygon_id, uint32_t>*  polygon_ids_uint32  {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Polygon_id, glm::vec3>* polygon_normals     {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Polygon_id, glm::vec3>* polygon_centroids   {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec3>*  corner_normals      {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec4>*  corner_tangents     {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec4>*  corner_bitangents   {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec2>*  corner_texcoords    {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec4>*  corner_colors       {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Corner_id, uint32_t>*   corner_indices      {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>*   point_locations     {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>*   point_normals       {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>*   point_normals_smooth{nullptr};
        erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec4>*   point_tangents      {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec4>*   point_bitangents    {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec2>*   point_texcoords     {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec4>*   point_colors        {nullptr};
    };

    Primitive_builder() = delete;

    Primitive_builder(const erhe::geometry::Geometry& geometry,
                      const Primitive_build_context&  context,
                      Normal_style                    normal_style);

    ~Primitive_builder();

    Primitive_geometry build();

    void build(Primitive_geometry* primitive_geometry);

    static void prepare_vertex_format(const Format_info& format_info,
                                      Buffer_info&       buffer_info);

private:
    struct Vertex_attributes
    {
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

    void get_vertex_attributes();
    void get_geometry_mesh_info();
    void allocate_vertex_buffer();
    void allocate_index_buffer();
    void calculate_bounding_box(erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>* point_locations);
    void allocate_index_range(gl::Primitive_type primitive_type,
                              size_t             index_count,
                              Index_range&       range);

    const erhe::geometry::Geometry& m_geometry;
    const Primitive_build_context&  m_context;
    Normal_style                    m_normal_style;
    Primitive_geometry*             m_primitive_geometry{nullptr};
    erhe::geometry::Mesh_info       m_mesh_info;
    erhe::graphics::Vertex_format*  m_vertex_format{nullptr};
    size_t                          m_vertex_stride{0};
    size_t                          m_total_vertex_count{0};
    size_t                          m_total_index_count{0};
    size_t                          m_next_index_range_start{0};
    Vertex_attributes               m_attributes;
};

auto make_primitive(const erhe::geometry::Geometry& geometry,
                    const Primitive_build_context&  context,
                    const Normal_style              normal_style = Normal_style::corner_normals)
-> Primitive_geometry;

auto make_primitive_shared(const erhe::geometry::Geometry& geometry,
                           const Primitive_build_context&  context,
                           const Normal_style              normal_style = Normal_style::corner_normals)
-> std::shared_ptr<Primitive_geometry>;

} // namespace erhe::primitive
