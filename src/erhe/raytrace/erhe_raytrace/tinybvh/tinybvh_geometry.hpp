#pragma once

#include "erhe_raytrace/igeometry.hpp"
#include "erhe_raytrace/prebuilt_bvh.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tinybvh {
    namespace impl {
        template <typename Float, typename Index> class BVH;
    }
    using BVH = impl::BVH<float, uint32_t>;
    struct bvhvec4;
}

namespace erhe::buffer {
    class Cpu_buffer;
}

namespace erhe::raytrace {

class Tinybvh_bvh;
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
    auto release_buffers           () -> bool override;
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
    auto get_triangle_hash         () const -> std::optional<uint64_t> override;
    auto get_triangle_count        () const -> std::size_t             override;
    auto export_bvh                (Prebuilt_bvh& out) const -> bool   override;
    auto set_prebuilt_bvh          (Prebuilt_bvh&& prebuilt) -> bool   override;
    auto get_bvh_source            () const -> Bvh_source              override;
    auto get_prebuilt_bvh_rejection() const -> std::string_view        override;

    // Tinybvh_geometry public API
    auto intersect_instance(Ray& ray, Hit& hit, Tinybvh_instance* instance) -> bool;
    auto get_bvh           () -> tinybvh::BVH*;
    auto get_triangles     () const -> const std::vector<tinybvh::bvhvec4>&;

private:
    [[nodiscard]] auto try_use_prebuilt_bvh(const Prebuilt_bvh& prebuilt, uint64_t hash_code, std::size_t triangle_count) -> bool;

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

    std::unique_ptr<Tinybvh_bvh>           m_bvh;
    std::vector<tinybvh::bvhvec4>          m_triangles;
    std::size_t                            m_triangle_count{0};
    std::optional<uint64_t>                m_triangle_hash {};
    Bvh_build_quality                      m_bvh_quality   {Bvh_build_quality::high};
    Bvh_source                             m_bvh_source    {Bvh_source::none};
    std::optional<Prebuilt_bvh>            m_prebuilt_bvh  {};
    std::string                            m_prebuilt_bvh_rejection;
};

} // namespace erhe::raytrace
