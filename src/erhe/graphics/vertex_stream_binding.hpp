

#pragma once

#include <cstddef>
#include <memory>

namespace erhe::graphics
{

class Buffer;
class Vertex_attribute;
class Vertex_attribute_mapping;

class Vertex_stream_binding
{
public:
    Vertex_stream_binding(
        Buffer*                                          vertex_buffer,
        const std::shared_ptr<Vertex_attribute_mapping>& mapping,
        const Vertex_attribute*                          attribute,
        const size_t                                     stride
    );

    Vertex_stream_binding(const Vertex_stream_binding& other);

    auto operator=(const Vertex_stream_binding& other)
        -> Vertex_stream_binding& = delete;

    Buffer*                                   vertex_buffer   {nullptr};
    std::shared_ptr<Vertex_attribute_mapping> vertex_attribute_mapping;
    const Vertex_attribute*                   vertex_attribute{nullptr};
    size_t                                    stride          {0};
};

} // namespace erhe::graphics
