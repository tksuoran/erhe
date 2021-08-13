#pragma once

#include "erhe/graphics/buffer.hpp"
#include "erhe/primitive/buffer_info.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/format_info.hpp"
#include "erhe/raytrace/device.hpp"

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::raytrace
{
    class Device;
}

namespace erhe::primitive
{

class Vertex_buffer_writer;
class Index_buffer_writer;

/// Queues byte buffers from Vertex_buffer_writer / Index_buffer_writer
/// to Buffer_transfer_queue.
class Geometry_uploader
{
public:
    Geometry_uploader(const Format_info& format_info,
                      const Buffer_info& buffer_info);
    ~Geometry_uploader();
    Geometry_uploader(const Geometry_uploader& other);
    void operator=   (Geometry_uploader&)  = delete;
    Geometry_uploader(Geometry_uploader&& other) noexcept;
    void operator=   (Geometry_uploader&&) = delete;

    virtual void end(Vertex_buffer_writer& writer) const = 0;
    virtual void end(Index_buffer_writer&  writer) const = 0;

    Format_info format_info;
    Buffer_info buffer_info;
};

class Gl_geometry_uploader
    : public Geometry_uploader
{
public:
    Gl_geometry_uploader(erhe::graphics::Buffer_transfer_queue& queue,
                         const Format_info&                     format_info,
                         const Buffer_info&                     buffer_info);
    ~Gl_geometry_uploader();
    Gl_geometry_uploader (const Gl_geometry_uploader& other);
    void operator=       (Gl_geometry_uploader&)  = delete;
    Gl_geometry_uploader (Gl_geometry_uploader&& other) noexcept;
    void operator=       (Gl_geometry_uploader&&) = delete;

    void end(Vertex_buffer_writer& writer) const override;
    void end(Index_buffer_writer&  writer) const override;

private:
    erhe::graphics::Buffer_transfer_queue& queue;
};

class Embree_geometry_uploader
    : public Geometry_uploader
{
public:
    Embree_geometry_uploader(erhe::raytrace::Device& device,
                             const Format_info&      format_info,
                             const Buffer_info&      buffer_info);
    ~Embree_geometry_uploader();
    Embree_geometry_uploader (const Embree_geometry_uploader& other);
    void operator=           (Embree_geometry_uploader&)  = delete;
    Embree_geometry_uploader (Embree_geometry_uploader&& other) noexcept;
    void operator=           (Embree_geometry_uploader&&) = delete;

    void end(Vertex_buffer_writer& writer) const override;
    void end(Index_buffer_writer&  writer) const override;

private:
    erhe::raytrace::Device& device;
};

} // namespace erhe::primitive
