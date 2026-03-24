#define TINYBVH_IMPLEMENTATION
#include "tiny_bvh.h"

#include <fmt/chrono.h>

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_file/file.hpp"
#include "erhe_raytrace/tinybvh/tinybvh_geometry.hpp"
#include "erhe_raytrace/tinybvh/tinybvh_instance.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/ray.hpp"

#include "erhe_hash/hash.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_time/timer.hpp"

#include <fstream>

namespace erhe::raytrace {

[[nodiscard]] auto get_tinybvh_cache_path(const uint64_t hash_code) -> std::filesystem::path
{
    const std::filesystem::path cache_path =
        std::filesystem::path{"cache"} / std::filesystem::path{"tinybvh"} / std::filesystem::path{ERHE_TINYBVH_GIT_COMMIT};
    const bool directory_ok = erhe::file::ensure_directory_exists(cache_path);
    if (!directory_ok) {
        return {};
    }
    const std::string hash_string = fmt::format("{}", hash_code);
    return cache_path / std::filesystem::path{hash_string};
}

auto save_tinybvh(tinybvh::BVH& bvh, const uint64_t hash_code) -> bool
{
    const std::filesystem::path cache_path = get_tinybvh_cache_path(hash_code);
    const std::string file_name = cache_path.string();
    if (file_name.empty()) {
        return false;
    }
    try {
        bvh.Save(file_name.c_str());
        return true;
    } catch (...) {
        return false;
    }
}

auto load_tinybvh(
    tinybvh::BVH&              bvh,
    const uint64_t             hash_code,
    const tinybvh::bvhvec4*    triangles,
    const uint32_t             triangle_count
) -> bool
{
    const std::filesystem::path cache_path = get_tinybvh_cache_path(hash_code);
    const std::string file_name = cache_path.string();
    if (file_name.empty()) {
        return false;
    }
    try {
        return bvh.Load(file_name.c_str(), triangles, triangle_count);
    } catch (...) {
        return false;
    }
}

auto IGeometry::create(const std::string_view debug_label, const Geometry_type geometry_type) -> IGeometry*
{
    return new Tinybvh_geometry(debug_label, geometry_type);
}

auto IGeometry::create_shared(const std::string_view debug_label, const Geometry_type geometry_type) -> std::shared_ptr<IGeometry>
{
    return std::make_shared<Tinybvh_geometry>(debug_label, geometry_type);
}

auto IGeometry::create_unique(const std::string_view debug_label, const Geometry_type geometry_type) -> std::unique_ptr<IGeometry>
{
    return std::make_unique<Tinybvh_geometry>(debug_label, geometry_type);
}

Tinybvh_geometry::Tinybvh_geometry(const std::string_view debug_label, const Geometry_type geometry_type)
    : m_debug_label{debug_label}
    , m_bvh{std::make_unique<tinybvh::BVH>()}
{
    static_cast<void>(geometry_type);
}

Tinybvh_geometry::~Tinybvh_geometry() noexcept = default;

void Tinybvh_geometry::commit()
{
    ERHE_PROFILE_FUNCTION();

    const Buffer_info* index_buffer_info{nullptr};
    const Buffer_info* vertex_buffer_info{nullptr};
    for (const Buffer_info& buffer : m_buffer_infos) {
        if (buffer.type == erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX) {
            index_buffer_info = &buffer;
            continue;
        }
        if (buffer.type == erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX) {
            vertex_buffer_info = &buffer;
            continue;
        }
    }
    if ((index_buffer_info == nullptr) || (vertex_buffer_info == nullptr)) {
        return;
    }

    if (vertex_buffer_info->format != erhe::dataformat::Format::format_32_vec3_float) {
        return;
    }

    if (index_buffer_info->format != erhe::dataformat::Format::format_32_vec3_uint) {
        return;
    }

    erhe::buffer::Cpu_buffer* index_buffer  = index_buffer_info->buffer;
    erhe::buffer::Cpu_buffer* vertex_buffer = vertex_buffer_info->buffer;
    if ((index_buffer == nullptr) || (vertex_buffer == nullptr)) {
        return;
    }

    const char* raw_index_ptr  = reinterpret_cast<char*>(index_buffer ->get_span().data()) + index_buffer_info ->byte_offset;
    const char* raw_vertex_ptr = reinterpret_cast<char*>(vertex_buffer->get_span().data()) + vertex_buffer_info->byte_offset;
    const std::size_t triangle_count = index_buffer_info->item_count;

    // Collect triangles and compute hash
    uint64_t hash_code{0xcbf29ce484222325};
    m_triangles.clear();
    m_triangles.reserve(triangle_count * 3);

    {
        ERHE_PROFILE_SCOPE("collect");

        for (std::size_t i = 0; i < triangle_count; ++i) {
            const uint32_t i0 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + (i * index_buffer_info->byte_stride) + (0 * sizeof(uint32_t)));
            const uint32_t i1 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + (i * index_buffer_info->byte_stride) + (1 * sizeof(uint32_t)));
            const uint32_t i2 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + (i * index_buffer_info->byte_stride) + (2 * sizeof(uint32_t)));

            const float p0_x = *reinterpret_cast<const float*>(raw_vertex_ptr + (i0 * vertex_buffer_info->byte_stride) + (0 * sizeof(float)));
            const float p0_y = *reinterpret_cast<const float*>(raw_vertex_ptr + (i0 * vertex_buffer_info->byte_stride) + (1 * sizeof(float)));
            const float p0_z = *reinterpret_cast<const float*>(raw_vertex_ptr + (i0 * vertex_buffer_info->byte_stride) + (2 * sizeof(float)));

            const float p1_x = *reinterpret_cast<const float*>(raw_vertex_ptr + (i1 * vertex_buffer_info->byte_stride) + (0 * sizeof(float)));
            const float p1_y = *reinterpret_cast<const float*>(raw_vertex_ptr + (i1 * vertex_buffer_info->byte_stride) + (1 * sizeof(float)));
            const float p1_z = *reinterpret_cast<const float*>(raw_vertex_ptr + (i1 * vertex_buffer_info->byte_stride) + (2 * sizeof(float)));

            const float p2_x = *reinterpret_cast<const float*>(raw_vertex_ptr + (i2 * vertex_buffer_info->byte_stride) + (0 * sizeof(float)));
            const float p2_y = *reinterpret_cast<const float*>(raw_vertex_ptr + (i2 * vertex_buffer_info->byte_stride) + (1 * sizeof(float)));
            const float p2_z = *reinterpret_cast<const float*>(raw_vertex_ptr + (i2 * vertex_buffer_info->byte_stride) + (2 * sizeof(float)));

            hash_code = erhe::hash::hash(p0_x, p0_y, p0_z, hash_code);
            hash_code = erhe::hash::hash(p1_x, p1_y, p1_z, hash_code);
            hash_code = erhe::hash::hash(p2_x, p2_y, p2_z, hash_code);

            // tinybvh expects 3 bvhvec4 per triangle (w is unused)
            m_triangles.push_back(tinybvh::bvhvec4{p0_x, p0_y, p0_z, 0.0f});
            m_triangles.push_back(tinybvh::bvhvec4{p1_x, p1_y, p1_z, 0.0f});
            m_triangles.push_back(tinybvh::bvhvec4{p2_x, p2_y, p2_z, 0.0f});
        }
        log_geometry->trace("tinybvh hash for {} : {:x}", debug_label(), hash_code);
    }

    m_triangle_count = triangle_count;

    const bool load_ok = load_tinybvh(*m_bvh, hash_code, m_triangles.data(), static_cast<uint32_t>(triangle_count));
    if (!load_ok) {
        ERHE_PROFILE_SCOPE("tinybvh build");
        erhe::time::Timer timer{m_debug_label.c_str()};

        timer.begin();
        m_bvh->Build(m_triangles.data(), static_cast<uint32_t>(triangle_count));
        timer.end();

        const long long time = std::chrono::duration_cast<std::chrono::milliseconds>(timer.duration().value()).count();
        log_geometry->info("tinybvh build {} in {} ms", debug_label(), time);

        const bool save_ok = save_tinybvh(*m_bvh, hash_code);
        if (!save_ok) {
            log_geometry->error("tinybvh save failed, hash = {}", hash_code);
        }
    }
}

void Tinybvh_geometry::enable()
{
    m_enabled = true;
}

void Tinybvh_geometry::disable()
{
    m_enabled = false;
}

auto Tinybvh_geometry::is_enabled() const -> bool
{
    return m_enabled;
}

void Tinybvh_geometry::set_mask(const uint32_t mask)
{
    m_mask = mask;
}

void Tinybvh_geometry::set_vertex_attribute_count(const unsigned int count)
{
    m_vertex_attribute_count = count;
}

void Tinybvh_geometry::set_buffer(
    const Buffer_type               type,
    const unsigned int              slot,
    const erhe::dataformat::Format  format,
    erhe::buffer::Cpu_buffer* const buffer,
    const std::size_t               byte_offset,
    const std::size_t               byte_stride,
    const std::size_t               item_count
)
{
    m_buffer_infos.push_back(
        Buffer_info{type, slot, format, buffer, byte_offset, byte_stride, item_count}
    );
}

void Tinybvh_geometry::set_user_data(const void* ptr)
{
    m_user_data = ptr;
}

auto Tinybvh_geometry::intersect_instance(Ray& ray, Hit& hit, Tinybvh_instance* instance) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return false;
    }
    if ((ray.mask & m_mask) == 0) {
        return false;
    }
    if (m_triangle_count == 0) {
        return false;
    }

    const glm::mat4 transform = (instance != nullptr) ? instance->get_transform() : glm::mat4{1.0f};

    tinybvh::Ray tinybvh_ray{
        tinybvh::bvhvec3{ray.origin.x,    ray.origin.y,    ray.origin.z},
        tinybvh::bvhvec3{ray.direction.x, ray.direction.y, ray.direction.z},
        ray.t_far
    };

    m_bvh->Intersect(tinybvh_ray);

    if (tinybvh_ray.hit.t < ray.t_far) {
        const uint32_t prim_id = tinybvh_ray.hit.prim;

        // Compute triangle normal from stored vertices
        const std::size_t base       = static_cast<std::size_t>(prim_id) * 3;
        const tinybvh::bvhvec4& v0   = m_triangles[base + 0];
        const tinybvh::bvhvec4& v1   = m_triangles[base + 1];
        const tinybvh::bvhvec4& v2   = m_triangles[base + 2];
        const glm::vec3 edge1        = glm::vec3{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
        const glm::vec3 edge2        = glm::vec3{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
        const glm::vec3 local_normal = glm::cross(edge1, edge2);

        ray.t_far       = tinybvh_ray.hit.t;
        hit.triangle_id = prim_id;
        hit.uv          = glm::vec2{tinybvh_ray.hit.u, tinybvh_ray.hit.v};
        hit.normal      = glm::vec3{transform * glm::vec4{local_normal, 0.0f}};
        hit.instance    = instance;
        hit.geometry    = this;
        return true;
    }
    return false;
}

auto Tinybvh_geometry::get_mask() const -> uint32_t
{
    return m_mask;
}

auto Tinybvh_geometry::get_user_data() const -> const void*
{
    return m_user_data;
}

auto Tinybvh_geometry::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace
