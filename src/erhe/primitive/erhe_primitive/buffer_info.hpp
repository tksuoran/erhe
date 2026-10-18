#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/mesh_optimizer.hpp"

#include <filesystem>

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

    // Mesh optimization for builds made through this Buffer_info. When set, a
    // geometry-path build also produces the optimized variant of the same mesh
    // (Primitive::optimized_render_shape) out of the bytes it already staged.
    //
    // It rides on Buffer_info rather than Build_info because the sink is what
    // the second variant has to be allocated from, and because the soup path
    // (which has no Build_info) needs the same answer. The value is read at
    // BUILD time, so a Buffer_info made after the Settings toggle changes
    // carries the new value - see Mesh_memory::make_primitive_buffer_info().
    bool                  optimize_meshes      {false};
    Mesh_optimize_options mesh_optimize_options{};

    // Filesystem cache of the geometry path's optimizer passes - see
    // optimize_indexed_mesh_cached(). Empty (the default) runs them uncached.
    // The owner of the build decides: the editor sets it for imported meshes
    // only, so procedural and edited geometry does not grow the cache.
    std::filesystem::path mesh_optimize_cache_directory{};

    // Vertex format the optimized variant is BUILT IN - the same content
    // attributes as vertex_format minus the per-corner facet id, which a welded
    // build cannot carry a meaningful value for (corners of different facets
    // merge into one vertex). Dropping the attribute, rather than keeping a
    // meaningless value in it, is what makes the optimized build unusable for
    // ID rendering by construction; it also takes those bytes out of the weld
    // compare and off the wire.
    //
    // A different format means a different Vertex_input_state, hence
    // optimized_vertex_input_key. Null disables the optimized build entirely -
    // which is the right answer for a sink that has no second format to offer
    // (CPU-buffer test sinks, the raytrace build).
    const erhe::dataformat::Vertex_format* optimized_vertex_format{nullptr};
    std::size_t                            optimized_vertex_input_key{0};
};

} // namesapce erhe::primitive
