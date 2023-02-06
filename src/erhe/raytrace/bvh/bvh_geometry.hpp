#pragma once

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include "erhe/raytrace/igeometry.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <glm/glm.hpp>

#include <bvh/v2/bvh.h>
#include <bvh/v2/vec.h>
#include <bvh/v2/tri.h>

#include <optional>
#include <string>
#include <vector>

namespace erhe::raytrace
{

class Bvh_instance;
class Bvh_scene;
class Ray;
class Hit;

class Bvh_geometry
    : public IGeometry
    ////, public erhe::toolkit::Bounding_volume_source
{
public:
    Bvh_geometry(
        const std::string_view debug_label,
        const Geometry_type    geometry_type
    );
    ~Bvh_geometry() noexcept override;

    // Implements IGeometry
    void commit                    () override;
    void enable                    () override;
    void disable                   () override;
    void set_mask                  (const uint32_t mask) override;
    void set_vertex_attribute_count(const unsigned int count) override;
    void set_buffer(
        Buffer_type  type,
        unsigned int slot,
        Format       format,
        IBuffer*     buffer,
        std::size_t  byte_offset,
        std::size_t  byte_stride,
        std::size_t  item_count
    ) override;
    void set_user_data(void* ptr) override;
    [[nodiscard]] auto get_mask     () const -> uint32_t         override;
    [[nodiscard]] auto get_user_data() const -> void*            override;
    [[nodiscard]] auto is_enabled   () const -> bool             override;
    [[nodiscard]] auto debug_label  () const -> std::string_view override;

    // Bvh_geometry public API
    auto intersect_instance(Ray& ray, Hit& hit, Bvh_instance* instance) -> bool;
    ////[[nodiscard]] auto get_sphere() const -> const erhe::toolkit::Bounding_sphere&;

    // Implements erhe::toolkit::Bounding_volume_source
    //// auto get_element_count      () const -> std::size_t override;
    //// auto get_element_point_count(std::size_t element_index) const -> std::size_t override;
    //// auto get_point              (std::size_t element_index, std::size_t point_index) const -> std::optional<glm::vec3> override;

private:
    class Buffer_info
    {
    public:
        Buffer_type  type       {Buffer_type::BUFFER_TYPE_INDEX};
        unsigned int slot       {0};
        Format       format     {Format::FORMAT_UNDEFINED};
        IBuffer*     buffer     {nullptr};
        std::size_t  byte_offset{0};
        std::size_t  byte_stride{0};
        std::size_t  item_count {0};
    };

    glm::mat4    m_transform  {1.0f};
    uint32_t     m_mask       {0xfffffffu};
    void*        m_user_data  {nullptr};
    std::string  m_debug_label;
    bool         m_enabled    {true};
    unsigned int m_vertex_attribute_count{0};

    std::vector<Buffer_info> m_buffer_infos;

    std::vector<bvh::v2::PrecomputedTri<float>> m_precomputed_triangles;
    bvh::v2::Bvh<bvh::v2::Node<float, 3>>       m_bvh;
};

}

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
