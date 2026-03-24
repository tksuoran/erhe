#pragma once

#include "erhe_raytrace/igeometry.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tinybvh {
    class BVH;
    struct bvhvec4;
}

namespace erhe::buffer {
    class Cpu_buffer;
}

namespace erhe::raytrace {

class Tinybvh_instance;
class Tinybvh_scene;
class Ray;
class Hit;

class Tinybvh_geometry : public IGeometry
{
public:
    Tinybvh_geometry(std::string_view debug_label, Geometry_type geometry_type);
    ~Tinybvh_geometry() noexcept override;

    // Implements IGeometry
    void commit                    () override;
    void enable                    () override;
    void disable                   () override;
    void set_mask                  (uint32_t mask) override;
    void set_vertex_attribute_count(unsigned int count) override;
    void set_buffer(
        Buffer_type               type,
        unsigned int              slot,
        erhe::dataformat::Format  format,
        erhe::buffer::Cpu_buffer* buffer,
        std::size_t               byte_offset,
        std::size_t               byte_stride,
        std::size_t               item_count
    ) override;
    void set_user_data(const void* ptr) override;
    auto get_mask     () const -> uint32_t         override;
    auto get_user_data() const -> const void*      override;
    auto is_enabled   () const -> bool             override;
    auto debug_label  () const -> std::string_view override;

    // Tinybvh_geometry public API
    auto intersect_instance(Ray& ray, Hit& hit, Tinybvh_instance* instance) -> bool;

private:
    class Buffer_info
    {
    public:
        Buffer_type               type       {Buffer_type::BUFFER_TYPE_INDEX};
        unsigned int              slot       {0};
        erhe::dataformat::Format  format     {erhe::dataformat::Format::format_undefined};
        erhe::buffer::Cpu_buffer* buffer     {nullptr};
        std::size_t               byte_offset{0};
        std::size_t               byte_stride{0};
        std::size_t               item_count {0};
    };

    uint32_t     m_mask                 {0xfffffffu};
    const void*  m_user_data            {nullptr};
    std::string  m_debug_label;
    bool         m_enabled              {true};
    unsigned int m_vertex_attribute_count{0};

    std::vector<Buffer_info> m_buffer_infos;

    std::unique_ptr<tinybvh::BVH>          m_bvh;
    std::vector<tinybvh::bvhvec4>          m_triangles;
    std::size_t                            m_triangle_count{0};
};

} // namespace erhe::raytrace
