#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/enums.hpp"

namespace erhe::primitive {

class Vertex_buffer_sink;
class Index_buffer_sink;

class Buffer_info
{
public:
    Normal_style                           normal_style{Normal_style::corner_normals};
    erhe::dataformat::Format               index_type  {erhe::dataformat::Format::format_16_scalar_uint};
    const erhe::dataformat::Vertex_format& vertex_format;
    Vertex_buffer_sink&                    vertex_buffer_sink;
    Index_buffer_sink&                     index_buffer_sink;
    size_t                                 vertex_input_key{0};

    // Optional separate stream descriptor used by Primitive_builder to
    // allocate Buffer_mesh::edge_line_vertex_buffer_range. When null, no
    // edge-line vertex buffer is allocated; consumers without a wide-line
    // path (e.g. CPU-buffer test sinks) leave this nullptr.
    const erhe::dataformat::Vertex_stream* edge_line_vertex_stream{nullptr};

    // Optional parallel stream descriptor for the edge-line joint side
    // buffer (uvec4 joint_indices + vec4 joint_weights per endpoint).
    // Primitive_builder allocates Buffer_mesh::edge_line_joint_buffer_range
    // from this stream only when the source GEO::Mesh has joint
    // attributes (skinned mesh). Leave nullptr to disable skinned edge
    // lines entirely.
    const erhe::dataformat::Vertex_stream* edge_line_joint_stream{nullptr};

    // Optional vertex format for the expanded solid-wireframe fill mesh
    // (same attributes as vertex_format plus custom_attribute_wireframe).
    // Primitive_builder allocates Buffer_mesh::expanded_vertex_buffer_ranges
    // from this format only when Primitive_types::fill_triangles_expanded is
    // set AND this is non-null. expanded_vertex_input_key is stored on the
    // Buffer_mesh so the renderer can bind the matching vertex input state
    // for the solid-wireframe draw. Leave nullptr to disable the expanded
    // build entirely (e.g. CPU-buffer test sinks).
    const erhe::dataformat::Vertex_format* expanded_vertex_format{nullptr};
    std::size_t                            expanded_vertex_input_key{0};
};

} // namesapce erhe::primitive
