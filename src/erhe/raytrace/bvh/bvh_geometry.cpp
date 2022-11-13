#include <fmt/chrono.h>

#include "erhe/raytrace/bvh/bvh_geometry.hpp"
#include "erhe/raytrace/bvh/bvh_instance.hpp"
#include "erhe/raytrace/bvh/bvh_scene.hpp"
#include "erhe/raytrace/bvh/glm_conversions.hpp"
#include "erhe/raytrace/ibuffer.hpp"
#include "erhe/raytrace/iinstance.hpp"
#include "erhe/raytrace/raytrace_log.hpp"
#include "erhe/raytrace/ray.hpp"

#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/timer.hpp"

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

Bvh_geometry::~Bvh_geometry() noexcept = default;

//// auto Bvh_geometry::get_element_count() const -> std::size_t
//// {
////     return 1;
//// }
//// 
//// auto Bvh_geometry::get_element_point_count(std::size_t element_index) const -> std::size_t
//// {
////     static_cast<void>(element_index);
////     return m_points.size();
//// }
//// 
//// auto Bvh_geometry::get_point(std::size_t element_index, std::size_t point_index) const -> std::optional<glm::vec3>
//// {
////     static_cast<void>(element_index);
////     if (point_index < m_points.size())
////     {
////         return m_points[point_index];
////     }
////     return {};
//// }

void Bvh_geometry::commit()
{
    ERHE_PROFILE_FUNCTION

    //// erhe::toolkit::Timer build_timer{"Bvh_geometry::commit()"};

    {
        //// erhe::toolkit::Scoped_timer scoped_timer{build_timer};

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
        m_triangles.reserve(index_buffer_info->item_count / 3);
        ////std::set<uint32_t> unique_indices;
        {
            ERHE_PROFILE_SCOPE("collect indices")

            for (std::size_t i = 0; i < index_buffer_info->item_count; ++i)
            {
                const uint32_t i0 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 0 * sizeof(uint32_t));
                const uint32_t i1 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 1 * sizeof(uint32_t));
                const uint32_t i2 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 2 * sizeof(uint32_t));

                ////unique_indices.insert(i0);
                ////unique_indices.insert(i1);
                ////unique_indices.insert(i2);

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
        }

        //// {
        ////     ERHE_PROFILE_SCOPE("collect vertices")
        ////     for (const auto i : unique_indices)
        ////     {
        ////         const float x = *reinterpret_cast<const float*>(raw_vertex_ptr + i * index_buffer_info->byte_stride + 0 * sizeof(float));
        ////         const float y = *reinterpret_cast<const float*>(raw_vertex_ptr + i * index_buffer_info->byte_stride + 1 * sizeof(float));
        ////         const float z = *reinterpret_cast<const float*>(raw_vertex_ptr + i * index_buffer_info->byte_stride + 2 * sizeof(float));
        //// 
        ////         m_points.emplace_back(x, y, z);
        ////     }
        //// }

        {
            ERHE_PROFILE_SCOPE("bbox")
            auto [bboxes, centers] = bvh::compute_bounding_boxes_and_centers<bvh::Triangle<float>>(
                m_triangles.data(),
                m_triangles.size()
            );
            m_bounding_boxes = std::move(bboxes);
            m_centers        = std::move(centers);
        }
        {
            ERHE_PROFILE_SCOPE("bbox union")
            m_global_bbox    = bvh::compute_bounding_boxes_union<float>(
                m_bounding_boxes.get(),
                m_triangles.size()
            );
        }

        // Create an acceleration data structure on the primitives
        {
            ERHE_PROFILE_SCOPE("SweepSahBuilder")
            bvh::SweepSahBuilder<bvh::Bvh<float>> builder{m_bvh};
            builder.build(
                m_global_bbox,
                m_bounding_boxes.get(),
                m_centers.get(),
                m_triangles.size()
            );
        }

        //// erhe::toolkit::calculate_bounding_volume(*this, m_bounding_box, m_bounding_sphere);
    }

    //// const auto duration = build_timer.duration().value();
    //// if (duration >= std::chrono::milliseconds(1))
    //// {
    ////     log_geometry->trace("build time:             {}", std::chrono::duration_cast<std::chrono::milliseconds>(build_timer.duration().value()));
    //// }
    //// else if (duration >= std::chrono::microseconds(1))
    //// {
    ////     log_geometry->trace("build time:             {}", std::chrono::duration_cast<std::chrono::microseconds>(build_timer.duration().value()));
    //// }
    //// else
    //// {
    ////     log_geometry->trace("build time:             {}", build_timer.duration().value());
    //// }
    //// log_geometry->trace("bvh triangle count:     {}", m_triangles.size());
    //// log_geometry->trace("bvh point count:        {}", m_points.size());
    //// log_geometry->trace("bounding box volume:    {}", m_bounding_box.volume());
    //// log_geometry->trace("bounding sphere volume: {}", m_bounding_sphere.volume());
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
    const std::size_t  byte_offset,
    const std::size_t  byte_stride,
    const std::size_t  item_count
)
{
    m_buffer_infos.push_back(
        Buffer_info{
            type, slot, format, buffer, byte_offset, byte_stride, item_count
        }
    );
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

/// auto Bvh_geometry::get_sphere() const -> const erhe::toolkit::Bounding_sphere&
/// {
///     return m_bounding_sphere;
/// }

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
