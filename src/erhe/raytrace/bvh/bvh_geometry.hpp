#pragma once

#include "erhe/raytrace/igeometry.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <glm/glm.hpp>

#include <bvh/bvh.hpp>
#include <bvh/vector.hpp>
#include <bvh/triangle.hpp>

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
    explicit Bvh_geometry(
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
        const Buffer_type  type,
        const unsigned int slot,
        const Format       format,
        IBuffer* const     buffer,
        const std::size_t  byte_offset,
        const std::size_t  byte_stride,
        const std::size_t  item_count
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

    std::vector<bvh::Triangle<float>>          m_triangles;
    ////std::vector<glm::vec3>                     m_points;
    std::unique_ptr<bvh::BoundingBox<float>[]> m_bounding_boxes;
    std::unique_ptr<bvh::Vector3<float>[]>     m_centers;
    bvh::BoundingBox<float>                    m_global_bbox;
    bvh::Bvh<float>                            m_bvh;
    erhe::toolkit::Bounding_box                m_bounding_box   {};
    erhe::toolkit::Bounding_sphere             m_bounding_sphere{};
};

}
