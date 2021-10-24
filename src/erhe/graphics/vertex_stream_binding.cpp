#include "erhe/graphics/vertex_stream_binding.hpp"

namespace erhe::graphics
{

Vertex_stream_binding::Vertex_stream_binding(
    Buffer*                                          vertex_buffer,
    const std::shared_ptr<Vertex_attribute_mapping>& mapping,
    const Vertex_attribute*                          attribute,
    const size_t                                     stride
)
    : vertex_buffer           {vertex_buffer}
    , vertex_attribute_mapping{mapping}
    , vertex_attribute        {attribute}
    , stride                  {stride}
{
}

Vertex_stream_binding::Vertex_stream_binding(const Vertex_stream_binding& other)
    : vertex_buffer           {other.vertex_buffer}
    , vertex_attribute_mapping{other.vertex_attribute_mapping}
    , vertex_attribute        {other.vertex_attribute}
    , stride                  {other.stride}
{
}

}