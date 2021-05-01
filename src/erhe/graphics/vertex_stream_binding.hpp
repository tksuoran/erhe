#ifndef vertex_stream_binding_hpp_erhe_graphics
#define vertex_stream_binding_hpp_erhe_graphics

#include <cstddef>

namespace erhe::graphics
{

class Buffer;
class Vertex_attribute;
class Vertex_attribute_mapping;

class Vertex_stream_binding
{
public:
    Vertex_stream_binding(Buffer*                         vertex_buffer,
                          const Vertex_attribute_mapping& mapping,
                          const Vertex_attribute*         attribute,
                          size_t                          stride)
        : vertex_buffer           {vertex_buffer}
        , vertex_attribute_mapping{mapping}
        , vertex_attribute        {attribute}
        , stride                  {stride}
    {
    }

    Vertex_stream_binding(const Vertex_stream_binding& other)
        : vertex_buffer           {other.vertex_buffer}
        , vertex_attribute_mapping{other.vertex_attribute_mapping}
        , vertex_attribute        {other.vertex_attribute}
        , stride                  {other.stride}
    {
    }

    auto operator=(const Vertex_stream_binding& other)
    -> Vertex_stream_binding& = delete;

    Buffer*                         vertex_buffer   {nullptr};
    const Vertex_attribute_mapping& vertex_attribute_mapping;
    const Vertex_attribute*         vertex_attribute{nullptr};
    size_t                          stride          {0};
};

} // namespace erhe::graphics

#endif
