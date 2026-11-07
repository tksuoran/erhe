#define TINYBVH_IMPLEMENTATION
#include "tiny_bvh.h"

#include <fmt/chrono.h>

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_raytrace/tinybvh/tinybvh_geometry.hpp"
#include "erhe_raytrace/tinybvh/tinybvh_instance.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/prebuilt_bvh.hpp"
#include "erhe_raytrace/ray.hpp"

#include "erhe_hash/hash.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_time/timer.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtx/matrix_operation.hpp>

#include <cstring>

namespace erhe::raytrace {

static_assert(tinybvh_traversal_stack_size == TINYBVH_STACK_SIZE);
#if defined(NORMALIZE_RAY_DIR)
#   error Tinybvh_geometry::intersect_instance() and Tinybvh_scene::intersect_tlas() expect t in the units of the given direction
#endif
static_assert(sizeof(tinybvh::BVH::BVHNode) == sizeof(Tinybvh_node_record));

// The pool operations Tinybvh_geometry needs set tinybvh::BVH's protected build flags.
class Tinybvh_bvh : public tinybvh::BVH
{
public:
    void release_pools();
    void install_prebuilt(const Prebuilt_bvh& prebuilt, const tinybvh::bvhvec4slice& vertices);
};

// The object stays (a scene's TLAS names it); Build() or the import allocates anew.
void Tinybvh_bvh::release_pools()
{
    AlignedFree(bvhNode);
    AlignedFree(primIdx);
    AlignedFree(fragment);
    bvhNode        = nullptr;
    primIdx        = nullptr;
    fragment       = nullptr;
    allocatedNodes = 0;
    usedNodes      = 0;
    newNodePtr     = 0;
    triCount       = 0;
    idxCount       = 0;
    verts          = tinybvh::bvhvec4slice{};
    rebuildable    = true;
    refittable     = true;
    hasHoles       = false;
    orderedNodes   = true;
}

// As BVH::Load() installs a BVH from its file: no fragments, so not rebuildable.
void Tinybvh_bvh::install_prebuilt(const Prebuilt_bvh& prebuilt, const tinybvh::bvhvec4slice& vertices)
{
    const uint32_t node_count     = static_cast<uint32_t>(prebuilt.tinybvh_nodes.size());
    const uint32_t triangle_count = static_cast<uint32_t>(prebuilt.primitive_ids.size());
    bvhNode        = static_cast<BVHNode*>(AlignedAlloc(node_count * sizeof(BVHNode)));
    primIdx        = static_cast<uint32_t*>(AlignedAlloc(triangle_count * sizeof(uint32_t)));
    std::memcpy(bvhNode, prebuilt.tinybvh_nodes.data(), node_count * sizeof(BVHNode));
    std::memcpy(primIdx, prebuilt.primitive_ids.data(), triangle_count * sizeof(uint32_t));
    allocatedNodes = node_count;
    usedNodes      = node_count;
    newNodePtr     = node_count;
    triCount       = triangle_count;
    idxCount       = triangle_count;
    verts          = vertices;
    aabbMin        = bvhNode[0].aabbMin;
    aabbMax        = bvhNode[0].aabbMax;
    rebuildable    = false;
    // The records do not say how the exporting build ordered and filled the pool.
    refittable     = false;
    hasHoles       = true;
    orderedNodes   = false;
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
    , m_bvh{std::make_unique<Tinybvh_bvh>()}
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

    // Collect triangles and compute hash (Prebuilt_bvh::triangle_hash)
    uint64_t hash_code{erhe::hash::c_seed};
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

            hash_code = hash_triangle_positions(glm::vec3{p0_x, p0_y, p0_z}, glm::vec3{p1_x, p1_y, p1_z}, glm::vec3{p2_x, p2_y, p2_z}, hash_code);

            // tinybvh expects 3 bvhvec4 per triangle (w is unused)
            m_triangles.push_back(tinybvh::bvhvec4{p0_x, p0_y, p0_z, 0.0f});
            m_triangles.push_back(tinybvh::bvhvec4{p1_x, p1_y, p1_z, 0.0f});
            m_triangles.push_back(tinybvh::bvhvec4{p2_x, p2_y, p2_z, 0.0f});
        }
        log_geometry->trace("tinybvh hash for {} : {:x}", debug_label(), hash_code);
    }

    m_triangle_count = triangle_count;
    m_triangle_hash  = hash_code;
    m_bvh->release_pools();

    // The supplied BVH is for this commit only.
    std::optional<Prebuilt_bvh> prebuilt = std::move(m_prebuilt_bvh);
    m_prebuilt_bvh.reset();
    m_prebuilt_bvh_rejection.clear();

    if (prebuilt.has_value() && try_use_prebuilt_bvh(prebuilt.value(), hash_code, triangle_count)) {
        m_bvh_source  = Bvh_source::prebuilt;
        m_bvh_quality = prebuilt.value().quality;
        return;
    }

    {
        ERHE_PROFILE_SCOPE("tinybvh build");
        erhe::time::Timer timer{m_debug_label.c_str()};
        timer.begin();
        m_bvh->Build(m_triangles.data(), static_cast<uint32_t>(triangle_count));
        timer.end();
        const long long time = std::chrono::duration_cast<std::chrono::milliseconds>(timer.duration().value()).count();
        log_geometry->info("tinybvh build {} in {} ms", debug_label(), time);
    }
    m_bvh_source  = Bvh_source::built;
    m_bvh_quality = Bvh_build_quality::high;
}

// Installs a validated prebuilt BVH (doc/erhe/raytrace.md "Prebuilt BVH") into
// m_bvh, whose pools commit() freed, as tinybvh::BVH::Load() does from its
// file: the node pool and the primitive indices over m_triangles.
auto Tinybvh_geometry::try_use_prebuilt_bvh(const Prebuilt_bvh& prebuilt, const uint64_t hash_code, const std::size_t triangle_count) -> bool
{
    ERHE_PROFILE_FUNCTION();

    const std::string reason = validate_prebuilt_bvh(
        prebuilt, get_prebuilt_bvh_backend(), get_prebuilt_bvh_library(), hash_code, triangle_count, tinybvh_traversal_stack_size
    );
    if (!reason.empty()) {
        log_geometry->warn("Prebuilt BVH for {} rejected: {}", debug_label(), reason);
        m_prebuilt_bvh_rejection = reason;
        return false;
    }
    m_bvh->install_prebuilt(
        prebuilt,
        tinybvh::bvhvec4slice{m_triangles.data(), static_cast<uint32_t>(triangle_count * 3), sizeof(tinybvh::bvhvec4)}
    );
    return true;
}

auto Tinybvh_geometry::set_prebuilt_bvh(Prebuilt_bvh&& prebuilt) -> bool
{
    m_prebuilt_bvh = std::move(prebuilt);
    return true;
}

auto Tinybvh_geometry::export_bvh(Prebuilt_bvh& out) const -> bool
{
    if ((m_bvh_source == Bvh_source::none) || !m_triangle_hash.has_value() || !m_bvh || (m_bvh->usedNodes == 0)) {
        return false;
    }
    ERHE_VERIFY(m_bvh->idxCount == m_triangle_count);
    out.backend        = std::string{get_prebuilt_bvh_backend()};
    out.library        = std::string{get_prebuilt_bvh_library()};
    out.quality        = m_bvh_quality;
    out.triangle_hash  = m_triangle_hash.value();
    out.triangle_count = m_triangle_count;
    out.nodes.clear();
    out.tinybvh_nodes.resize(m_bvh->usedNodes);
    std::memcpy(out.tinybvh_nodes.data(), m_bvh->bvhNode, m_bvh->usedNodes * sizeof(Tinybvh_node_record));
    // Node 1 and the holes of a threaded build hold whatever the pool held; zero them for stable bytes.
    // A walk from the root: a Bonsai build places children before their parents.
    std::vector<uint8_t>  referenced(m_bvh->usedNodes, 0);
    std::vector<uint32_t> stack{0u};
    referenced[0] = 1;
    while (!stack.empty()) {
        const Tinybvh_node_record& node = out.tinybvh_nodes[stack.back()];
        stack.pop_back();
        if ((node.tri_count == 0) && ((node.left_first + 1) < m_bvh->usedNodes)) {
            for (const uint32_t child : {node.left_first, node.left_first + 1}) {
                if (referenced[child] == 0) {
                    referenced[child] = 1;
                    stack.push_back(child);
                }
            }
        }
    }
    for (uint32_t n = 0; n < m_bvh->usedNodes; ++n) {
        if (referenced[n] == 0) {
            out.tinybvh_nodes[n] = Tinybvh_node_record{};
        }
    }
    out.primitive_ids.assign(m_bvh->primIdx, m_bvh->primIdx + m_bvh->idxCount);
    return true;
}

auto Tinybvh_geometry::get_triangle_hash() const -> std::optional<uint64_t>
{
    return m_triangle_hash;
}

auto Tinybvh_geometry::get_bvh_source() const -> Bvh_source
{
    return m_bvh_source;
}

auto Tinybvh_geometry::get_prebuilt_bvh_rejection() const -> std::string_view
{
    return m_prebuilt_bvh_rejection;
}

// commit() copies what traversal and hit reporting read (the precomputed
// triangles / the triangle array and the BVH); nothing reads the caller's
// buffers afterwards.
auto Tinybvh_geometry::release_buffers() -> bool
{
    m_buffer_infos.clear();
    return true;
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

    // tinybvh keeps the direction as given (NORMALIZE_RAY_DIR undefined), so t is in the caller's units.
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
        // A normal is a covector: transforming it by the instance transform
        // shears it whenever the scale is non-uniform, tilting it toward the
        // longest axis. transpose(adjugate(M)) is the cofactor matrix, the
        // same normal transform used throughout the renderer.
        hit.normal      = glm::vec3{glm::transpose(glm::adjugate(transform)) * glm::vec4{local_normal, 0.0f}};
        hit.instance    = instance;
        hit.geometry    = this;
        return true;
    }
    return false;
}

auto Tinybvh_geometry::get_bvh() -> tinybvh::BVH*
{
    return m_bvh.get();
}

auto Tinybvh_geometry::get_triangle_count() const -> std::size_t
{
    return m_triangle_count;
}

auto Tinybvh_geometry::get_triangles() const -> const std::vector<tinybvh::bvhvec4>&
{
    return m_triangles;
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
