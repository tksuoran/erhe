#pragma once

#include "erhe/primitive/buffer_range.hpp"

#include <gsl/span>

#include <memory>

namespace erhe::graphics
{
    class Buffer;
    class Buffer_transfer_queue;
}

namespace erhe::raytrace
{
    class Buffer;
}

namespace erhe::primitive
{

class Build_context;
class Index_buffer_writer;
class Vertex_buffer_writer;

class Buffer_sink
{
public:
    virtual auto allocate_vertex_buffer(const size_t vertex_count,
                                        const size_t vertex_element_size) -> Buffer_range = 0;

    virtual auto allocate_index_buffer(const size_t index_count,
                                       const size_t index_element_size) -> Buffer_range = 0;

    virtual void buffer_ready(Vertex_buffer_writer& writer) const = 0;
    virtual void buffer_ready(Index_buffer_writer&  writer) const = 0;
};

class Gl_buffer_sink
    : public Buffer_sink
{
public:
    Gl_buffer_sink(erhe::graphics::Buffer_transfer_queue&         buffer_transfer_queue,
                   const std::shared_ptr<erhe::graphics::Buffer>& vertex_buffer,
                   const std::shared_ptr<erhe::graphics::Buffer>& index_buffer);

    auto allocate_vertex_buffer(const size_t vertex_count,
                                const size_t vertex_element_size) -> Buffer_range override;

    auto allocate_index_buffer(const size_t index_count,
                               const size_t index_element_size) -> Buffer_range override;

    void buffer_ready(Vertex_buffer_writer& writer) const override;
    void buffer_ready(Index_buffer_writer&  writer) const override;

private:
    erhe::graphics::Buffer_transfer_queue&  m_buffer_transfer_queue;
    std::shared_ptr<erhe::graphics::Buffer> m_vertex_buffer;
    std::shared_ptr<erhe::graphics::Buffer> m_index_buffer;
};

class Embree_buffer_sink
    : public Buffer_sink
{
public:
    Embree_buffer_sink(const std::shared_ptr<erhe::raytrace::Buffer>& vertex_buffer,
                       const std::shared_ptr<erhe::raytrace::Buffer>& index_buffer);

    auto allocate_vertex_buffer(const size_t vertex_count,
                                const size_t vertex_element_size) -> Buffer_range override;

    auto allocate_index_buffer(const size_t index_count,
                               const size_t index_element_size) -> Buffer_range override;

    void buffer_ready(Vertex_buffer_writer& writer) const override;
    void buffer_ready(Index_buffer_writer&  writer) const override;

private:
    std::shared_ptr<erhe::raytrace::Buffer> m_vertex_buffer;
    std::shared_ptr<erhe::raytrace::Buffer> m_index_buffer;
};

} // namespace erhe::primitive
