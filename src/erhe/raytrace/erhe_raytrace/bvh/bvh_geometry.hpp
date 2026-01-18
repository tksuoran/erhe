#pragma once

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include "erhe_raytrace/igeometry.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <glm/glm.hpp>

#include <bvh/v2/bvh.h>
#include <bvh/v2/tri.h>

#include <string>
#include <vector>

namespace erhe::buffer {
    class Cpu_buffer;
}

namespace erhe::raytrace {

class Bvh_instance;
class Bvh_scene;
class Ray;
class Hit;

class Bvh_geometry : public IGeometry
{
public:
    Bvh_geometry(std::string_view debug_label, Geometry_type geometry_type);
    ~Bvh_geometry() noexcept override;

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

    // Bvh_geometry public API
    auto intersect_instance(Ray& ray, Hit& hit, Bvh_instance* instance) -> bool;

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

    glm::mat4    m_transform  {1.0f};
    uint32_t     m_mask       {0xfffffffu};
    const void*  m_user_data  {nullptr};
    std::string  m_debug_label;
    bool         m_enabled    {true};
    unsigned int m_vertex_attribute_count{0};

    std::vector<Buffer_info> m_buffer_infos;

    std::vector<bvh::v2::PrecomputedTri<float>> m_precomputed_triangles;
    bvh::v2::Bvh<bvh::v2::Node<float, 3>>       m_bvh;
};

} // namespace erhe::raytrace

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
