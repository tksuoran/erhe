#pragma once

#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace erhe::dataformat {
    class Vertex_format;
}

namespace erhe::primitive {

class Prepared_primitive_data;

// The layouts a loader asks a source for when it would take GPU-ready vertex
// streams stored in the file (ERHE_gpu_vertex_streams,
// doc/gltf_extensions/ERHE_gpu_vertex_streams.md): the running base and
// optimized vertex formats, the index format and the encoder version. A
// stored variant is used only when its signature
// (erhe::dataformat::get_vertex_format_signature()) and encoding version
// equal these.
class Stored_vertex_stream_request
{
public:
    const erhe::dataformat::Vertex_format* base_format     {nullptr};
    const erhe::dataformat::Vertex_format* optimized_format{nullptr}; // null: the base variant only
    erhe::dataformat::Format               index_type      {erhe::dataformat::Format::format_32_scalar_uint};
    int                                    encoding_version{0};
};

// One variant's vertex streams, one byte vector per stream of the requested
// format (count * stride bytes; a stream the file declares zero-filled is
// zeros), enqueued to the GPU as they are.
class Stored_vertex_variant
{
public:
    std::vector<std::vector<uint8_t>> streams;
};

// What a source read for a stored build: the indices (index_type), the base
// and optional optimized streams, the float positions of the core (for
// the bounds and the raytrace), the affines the optimized streams were
// encoded with (bit-exact, as stored), and the validated prepared mesh data.
class Stored_vertex_streams
{
public:
    std::size_t                              vertex_count{0};
    std::size_t                              index_count {0};
    std::vector<uint8_t>                     indices;
    std::vector<float>                       positions_xyz;
    Stored_vertex_variant                    base;
    std::optional<Stored_vertex_variant>     optimized;
    Position_quantization                    position_quantization{};
    Texcoord_quantization                    texcoord_quantization{};
    bool                                     has_vertex_colors{false};
    std::shared_ptr<Prepared_primitive_data> prepared_data;
};

} // namespace erhe::primitive
