#include "erhe/raytrace/bvh/bvh_geometry.hpp"
#include "erhe/raytrace/bvh/bvh_instance.hpp"
#include "erhe/raytrace/bvh/bvh_scene.hpp"
#include "erhe/raytrace/bvh/glm_conversions.hpp"
#include "erhe/raytrace/ibuffer.hpp"
#include "erhe/raytrace/iinstance.hpp"
#include "erhe/raytrace/log.hpp"
#include "erhe/raytrace/ray.hpp"

#include "erhe/log/log_fmt.hpp"
#include "erhe/toolkit/timer.hpp"

#include <fmt/chrono.h>

#include <bvh/sphere.hpp>
#include <bvh/ray.hpp>
#include <bvh/sweep_sah_builder.hpp>
#include <bvh/single_ray_traverser.hpp>
#include <bvh/primitive_intersectors.hpp>

#include <set>

namespace erhe::raytrace
{

auto IGeometry::create(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
) -> IGeometry*
{
    return new Bvh_geometry(debug_label, geometry_type);
}

auto IGeometry::create_shared(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
) -> std::shared_ptr<IGeometry>
{
    return std::make_shared<Bvh_geometry>(debug_label, geometry_type);
}

auto IGeometry::create_unique(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
) -> std::unique_ptr<IGeometry>
{
    return std::make_unique<Bvh_geometry>(debug_label, geometry_type);
}

Bvh_geometry::Bvh_geometry(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
)
    : m_debug_label{debug_label}
{
    static_cast<void>(geometry_type);
}

Bvh_geometry::~Bvh_geometry() = default;

auto Bvh_geometry::point_count() const -> size_t
{
    return m_points.size();
}

auto Bvh_geometry::get_point(size_t index) const -> std::optional<glm::vec3>
{
    if (index < m_points.size())
    {
        return m_points[index];
    }
    return {};
}

void Bvh_geometry::commit()
{
    erhe::toolkit::Timer build_timer{"Bvh_geometry::commit()"};

    {
        erhe::toolkit::Scoped_timer scoped_timer{build_timer};

        const Buffer_info* index_buffer_info{nullptr};
        const Buffer_info* vertex_buffer_info{nullptr};
        for (const auto& buffer : m_buffer_infos)
        {
            if (buffer.type == erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX)
            {
                index_buffer_info = &buffer;
                continue;
            }
            if (buffer.type == erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX)
            {
                vertex_buffer_info = &buffer;
                continue;
            }
        }
        if (
            (index_buffer_info == nullptr) ||
            (vertex_buffer_info == nullptr)
        )
        {
            return;
        }

        if (vertex_buffer_info->format != erhe::raytrace::Format::FORMAT_FLOAT3)
        {
            return;
        }

        if (index_buffer_info->format != erhe::raytrace::Format::FORMAT_UINT3)
        {
            return;
        }

        IBuffer* index_buffer  = index_buffer_info->buffer;
        IBuffer* vertex_buffer = vertex_buffer_info->buffer;
        if (
            (index_buffer == nullptr) ||
            (vertex_buffer == nullptr)
        )
        {
            return;
        }

        const char* raw_index_ptr  = reinterpret_cast<char*>(index_buffer ->span().data()) + index_buffer_info ->byte_offset;
        const char* raw_vertex_ptr = reinterpret_cast<char*>(vertex_buffer->span().data()) + vertex_buffer_info->byte_offset;

        m_triangles.clear();
        std::set<uint32_t> unique_indices;
        for (size_t i = 0; i < index_buffer_info->item_count; ++i)
        {
            const uint32_t i0 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 0 * sizeof(uint32_t));
            const uint32_t i1 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 1 * sizeof(uint32_t));
            const uint32_t i2 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 2 * sizeof(uint32_t));

            unique_indices.insert(i0);
            unique_indices.insert(i1);
            unique_indices.insert(i2);

            const float p0_x = *reinterpret_cast<const float*>(raw_vertex_ptr + i0 * index_buffer_info->byte_stride + 0 * sizeof(float));
            const float p0_y = *reinterpret_cast<const float*>(raw_vertex_ptr + i0 * index_buffer_info->byte_stride + 1 * sizeof(float));
            const float p0_z = *reinterpret_cast<const float*>(raw_vertex_ptr + i0 * index_buffer_info->byte_stride + 2 * sizeof(float));

            const float p1_x = *reinterpret_cast<const float*>(raw_vertex_ptr + i1 * index_buffer_info->byte_stride + 0 * sizeof(float));
            const float p1_y = *reinterpret_cast<const float*>(raw_vertex_ptr + i1 * index_buffer_info->byte_stride + 1 * sizeof(float));
            const float p1_z = *reinterpret_cast<const float*>(raw_vertex_ptr + i1 * index_buffer_info->byte_stride + 2 * sizeof(float));

            const float p2_x = *reinterpret_cast<const float*>(raw_vertex_ptr + i2 * index_buffer_info->byte_stride + 0 * sizeof(float));
            const float p2_y = *reinterpret_cast<const float*>(raw_vertex_ptr + i2 * index_buffer_info->byte_stride + 1 * sizeof(float));
            const float p2_z = *reinterpret_cast<const float*>(raw_vertex_ptr + i2 * index_buffer_info->byte_stride + 2 * sizeof(float));

            m_triangles.emplace_back(
                bvh::Vector3<float>(p0_x, p0_y, p0_z),
                bvh::Vector3<float>(p1_x, p1_y, p1_z),
                bvh::Vector3<float>(p2_x, p2_y, p2_z)
            );
        }

        for (const auto i : unique_indices)
        {
            const float x = *reinterpret_cast<const float*>(raw_vertex_ptr + i * index_buffer_info->byte_stride + 0 * sizeof(float));
            const float y = *reinterpret_cast<const float*>(raw_vertex_ptr + i * index_buffer_info->byte_stride + 1 * sizeof(float));
            const float z = *reinterpret_cast<const float*>(raw_vertex_ptr + i * index_buffer_info->byte_stride + 2 * sizeof(float));

            m_points.emplace_back(x, y, z);
        }

        auto [bboxes, centers] = bvh::compute_bounding_boxes_and_centers<bvh::Triangle<float>>(
            m_triangles.data(),
            m_triangles.size()
        );
        m_bounding_boxes = std::move(bboxes);
        m_centers        = std::move(centers);
        m_global_bbox    = bvh::compute_bounding_boxes_union<float>(
            m_bounding_boxes.get(),
            m_triangles.size()
        );

        // Create an acceleration data structure on the primitives
        bvh::SweepSahBuilder<bvh::Bvh<float>> builder{m_bvh};
        builder.build(
            m_global_bbox,
            m_bounding_boxes.get(),
            m_centers.get(),
            m_triangles.size()
        );

        erhe::toolkit::calculate_bounding_volume(*this, m_bounding_box, m_bounding_sphere);
    }

    const auto duration = build_timer.duration().value();
    if (duration >= std::chrono::milliseconds(1))
    {
        info_fmt(log_geometry, "build time:             {}\n", std::chrono::duration_cast<std::chrono::milliseconds>(build_timer.duration().value()));
    }
    else if (duration >= std::chrono::microseconds(1))
    {
        info_fmt(log_geometry, "build time:             {}\n", std::chrono::duration_cast<std::chrono::microseconds>(build_timer.duration().value()));
    }
    else
    {
        info_fmt(log_geometry, "build time:             {}\n", build_timer.duration().value());
    }
    info_fmt(log_geometry, "bvh triangle count:     {}\n", m_triangles.size());
    info_fmt(log_geometry, "bvh point count:        {}\n", m_points.size());
    info_fmt(log_geometry, "bounding box volume:    {}\n", m_bounding_box.volume());
    info_fmt(log_geometry, "bounding sphere volume: {}\n", m_bounding_sphere.volume());
}

void Bvh_geometry::enable()
{
    m_enabled = true;
}

void Bvh_geometry::disable()
{
    m_enabled = false;
}

auto Bvh_geometry::is_enabled() const -> bool
{
    return m_enabled;
}

void Bvh_geometry::set_mask(const uint32_t mask)
{
    m_mask = mask;
}

void Bvh_geometry::set_vertex_attribute_count(const unsigned int count)
{
    m_vertex_attribute_count = count;
}

void Bvh_geometry::set_buffer(
    const Buffer_type  type,
    const unsigned int slot,
    const Format       format,
    IBuffer* const     buffer,
    const size_t       byte_offset,
    const size_t       byte_stride,
    const size_t       item_count
)
{
    m_buffer_infos.emplace_back(type, slot, format, buffer, byte_offset, byte_stride, item_count);
}

void Bvh_geometry::set_user_data(void* ptr)
{
    m_user_data = ptr;
}

auto Bvh_geometry::intersect_instance(Ray& ray, Hit& hit, Bvh_instance* instance) -> bool
{
    if (!m_enabled)
    {
        return false;
    }
    if ((ray.mask & m_mask) == 0)
    {
        return false;
    }

    const auto transform = (instance != nullptr)
        ? instance->get_transform()
        : glm::mat4{1.0};
    bvh::Ray bvh_ray{
        to_bvh(ray.origin),
        to_bvh(ray.direction),
        ray.t_near,
        ray.t_far
    };

    bvh::ClosestPrimitiveIntersector<bvh::Bvh<float>, bvh::Triangle<float>> primitive_intersector{
        m_bvh,
        m_triangles.data()
    };
    bvh::SingleRayTraverser<bvh::Bvh<float>> traverser{m_bvh};

    if (auto bvh_hit = traverser.traverse(bvh_ray, primitive_intersector))
    {
        const auto triangle_index = bvh_hit->primitive_index;
        const auto intersection   = bvh_hit->intersection;

        const auto& triangle = m_triangles.at(triangle_index);

        ray.t_far        = intersection.t;
        hit.primitive_id = static_cast<unsigned int>(triangle_index);
        hit.uv           = glm::vec2{intersection.u, intersection.v};
        hit.normal       = glm::vec3{transform * glm::vec4{from_bvh(triangle.n), 0.0f}};
        hit.instance     = instance;
        hit.geometry     = this;
        return true;
    }
    return false;
}

auto Bvh_geometry::get_sphere() const -> const erhe::toolkit::Bounding_sphere&
{
    return m_bounding_sphere;
}

auto Bvh_geometry::get_mask() const -> uint32_t
{
    return m_mask;
}

auto Bvh_geometry::get_user_data() const -> void*
{
    return m_user_data;
}

auto Bvh_geometry::debug_label() const -> std::string_view
{
    return m_debug_label;
}


} // namespace erhe::raytrace
