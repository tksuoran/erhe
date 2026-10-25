#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include <fmt/chrono.h>

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_file/file.hpp"
#include "erhe_raytrace/bvh/bvh_geometry.hpp"
#include "erhe_raytrace/bvh/bvh_instance.hpp"
#include "erhe_raytrace/bvh/bvh_scene.hpp"
#include "erhe_raytrace/bvh/glm_conversions.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/ray.hpp"

#include "erhe_hash/hash.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_time/timer.hpp"

#include <glm/gtx/matrix_operation.hpp>

#include <bvh/v2/bvh.h>
#include <bvh/v2/default_builder.h>
#include <bvh/v2/executor.h>
#include <bvh/v2/node.h>
#include <bvh/v2/ray.h>
#include <bvh/v2/stack.h>
#include <bvh/v2/thread_pool.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <system_error>

namespace erhe::raytrace {

using Bvh = bvh::v2::Bvh<bvh::v2::Node<float, 3>>;

[[nodiscard]] auto get_bvh_cache_path(const uint64_t hash_code) -> std::filesystem::path
{
    // Keyed by the pinned library tag (a build-time constant, the same on
    // every platform), not by the checkout's git commit.
    const std::filesystem::path bvh_cache_path =
        std::filesystem::path{"cache"} / std::filesystem::path{"bvh"} / std::filesystem::path{ERHE_BVH_LIBRARY_TAG};
    const bool directory_ok = erhe::file::ensure_directory_exists(bvh_cache_path);
    if (!directory_ok) {
        return {};
    }
    const std::string hash_string = fmt::format("{}", hash_code);
    return bvh_cache_path / std::filesystem::path{hash_string};
}

// TODO Add versioning
auto save_bvh(const Bvh& bvh, const uint64_t hash_code) -> bool
{
    const std::filesystem::path cache_path = get_bvh_cache_path(hash_code);
    if (cache_path.empty()) {
        return false;
    }

    // Write to a process/thread unique temporary file and rename it in place, so that
    // readers never observe a partially written cache entry. Multiple geometries with
    // identical content hash to the same cache entry, and they can be committed
    // concurrently from task workers.
    static std::atomic<uint64_t> s_temp_counter{0};
    const std::filesystem::path temp_path = cache_path.parent_path() / std::filesystem::path{
        fmt::format("{}.{}.tmp", cache_path.filename().string(), s_temp_counter.fetch_add(1))
    };

    {
        std::ofstream out{temp_path.string(), std::ofstream::binary};
        if (!out) {
            return false;
        }
        try {
            bvh::v2::StdOutputStream stream{out};
            bvh.serialize(stream);
        } catch (...) {
            out.close();
            std::error_code discarded_error_code{};
            std::filesystem::remove(temp_path, discarded_error_code);
            return false;
        }
        out.close();
        if (!out) {
            std::error_code discarded_error_code{};
            std::filesystem::remove(temp_path, discarded_error_code);
            return false;
        }
    }

    std::error_code error_code{};
    std::filesystem::rename(temp_path, cache_path, error_code);
    if (error_code) {
        std::error_code discarded_error_code{};
        std::filesystem::remove(temp_path, discarded_error_code);
        return false;
    }
    return true;
}

// TODO Add versioning
auto load_bvh(Bvh& bvh, const uint64_t hash_code, const std::size_t primitive_count) -> bool
{
    const std::filesystem::path cache_path = get_bvh_cache_path(hash_code);
    const std::string file_name = cache_path.string();
    if (file_name.empty()) {
        return false;
    }
    std::ifstream in{file_name, std::ofstream::binary};
    if (!in) {
        return false;
    }
    Bvh loaded_bvh{};
    try {
        bvh::v2::StdInputStream stream{in};
        loaded_bvh = Bvh::deserialize(stream);
    } catch (...) {
        return false;
    }

    // Bvh::deserialize() does not report short reads: a truncated or empty cache entry
    // simply yields empty / partial node and primitive id arrays. Validate the result
    // instead of trusting the file.
    if (loaded_bvh.nodes.empty() || (loaded_bvh.prim_ids.size() != primitive_count)) {
        log_geometry->warn("Ignoring invalid BVH cache entry {}", file_name);
        return false;
    }
    for (const std::size_t prim_id : loaded_bvh.prim_ids) {
        if (prim_id >= primitive_count) {
            log_geometry->warn("Ignoring invalid BVH cache entry {}", file_name);
            return false;
        }
    }

    bvh = std::move(loaded_bvh);
    return true;
}

auto IGeometry::create(const std::string_view debug_label, const Geometry_type geometry_type) -> IGeometry*
{
    return new Bvh_geometry(debug_label, geometry_type);
}

auto IGeometry::create_shared(const std::string_view debug_label, const Geometry_type geometry_type) -> std::shared_ptr<IGeometry>
{
    return std::make_shared<Bvh_geometry>(debug_label, geometry_type);
}

auto IGeometry::create_unique(const std::string_view debug_label, const Geometry_type geometry_type) -> std::unique_ptr<IGeometry>
{
    return std::make_unique<Bvh_geometry>(debug_label, geometry_type);
}

Bvh_geometry::Bvh_geometry(const std::string_view debug_label, const Geometry_type geometry_type)
    : m_debug_label{debug_label}
{
    static_cast<void>(geometry_type);
}

Bvh_geometry::~Bvh_geometry() noexcept
{
    // Scenes hold raw pointers to their children, so a geometry which is
    // destroyed while still attached has to remove itself.
    const std::vector<Bvh_scene*> parent_scenes = m_parent_scenes;
    m_parent_scenes.clear();
    for (Bvh_scene* scene : parent_scenes) {
        scene->on_child_destroyed(this);
    }
}

void Bvh_geometry::add_parent_scene(Bvh_scene* scene)
{
    const auto i = std::find(m_parent_scenes.begin(), m_parent_scenes.end(), scene);
    if (i == m_parent_scenes.end()) {
        m_parent_scenes.push_back(scene);
    }
}

void Bvh_geometry::remove_parent_scene(Bvh_scene* scene)
{
    const auto i = std::find(m_parent_scenes.begin(), m_parent_scenes.end(), scene);
    if (i != m_parent_scenes.end()) {
        m_parent_scenes.erase(i);
    }
}

void Bvh_geometry::notify_parents_modified()
{
    for (Bvh_scene* scene : m_parent_scenes) {
        scene->on_child_modified(this);
    }
}

using Scalar         = float;
using Vec3           = bvh::v2::Vec<Scalar, 3>;
using BBox           = bvh::v2::BBox<Scalar, 3>;
using Tri            = bvh::v2::Tri<Scalar, 3>;
using Node           = bvh::v2::Node<Scalar, 3>;
using Bvh            = bvh::v2::Bvh<Node>;
using PrecomputedTri = bvh::v2::PrecomputedTri<Scalar>;

static constexpr bool should_permute = true; // TODO

// Traversal stack capacity in intersect_instance(); also the deepest a
// prebuilt BVH may be (inner ancestors of any node).
static constexpr std::size_t c_traversal_stack_size = 64;

static_assert(sizeof(Node) == sizeof(Bvh_node_record));
static_assert(sizeof(Node::Index::Type) == sizeof(uint32_t));
static_assert(Node::Index::prim_count_bits == 4);

class Executor_resources
{
public:
    static Executor_resources& get_instance() {
        static Executor_resources static_instance;
        return static_instance;
    }

    auto get_thread_pool() -> bvh::v2::ThreadPool&       { return m_thread_pool; }
    auto get_executor   () -> bvh::v2::ParallelExecutor& { return m_executor; }

private:
    Executor_resources() : m_thread_pool{}, m_executor{m_thread_pool} {}
    ~Executor_resources() noexcept {};

    bvh::v2::ThreadPool       m_thread_pool;
    bvh::v2::ParallelExecutor m_executor;
};

void Bvh_geometry::commit()
{
    ERHE_PROFILE_FUNCTION();

    // A supplied BVH applies to this commit() only.
    const std::optional<Prebuilt_bvh> prebuilt = std::move(m_prebuilt_bvh);
    m_prebuilt_bvh.reset();
    m_prebuilt_bvh_rejection.clear();
    m_bvh_source     = Bvh_source::none;
    m_triangle_hash.reset();
    m_triangle_count = 0;

    {
        const Buffer_info* index_buffer_info{nullptr};
        const Buffer_info* vertex_buffer_info{nullptr};
        for (const auto& buffer : m_buffer_infos) {
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

        std::vector<Tri> tris;

        uint64_t hash_code{0xcbf29ce484222325};
        std::vector<BBox> bboxes(triangle_count);
        std::vector<Vec3> centers(triangle_count);
        m_bbox = erhe::math::Aabb{};
        {
            ERHE_PROFILE_SCOPE("collect");

            for (std::size_t i = 0; i < triangle_count; ++i) {
                const uint32_t i0 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 0 * sizeof(uint32_t));
                const uint32_t i1 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 1 * sizeof(uint32_t));
                const uint32_t i2 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 2 * sizeof(uint32_t));

                const float p0_x = *reinterpret_cast<const float*>(raw_vertex_ptr + i0 * vertex_buffer_info->byte_stride + 0 * sizeof(float));
                const float p0_y = *reinterpret_cast<const float*>(raw_vertex_ptr + i0 * vertex_buffer_info->byte_stride + 1 * sizeof(float));
                const float p0_z = *reinterpret_cast<const float*>(raw_vertex_ptr + i0 * vertex_buffer_info->byte_stride + 2 * sizeof(float));

                const float p1_x = *reinterpret_cast<const float*>(raw_vertex_ptr + i1 * vertex_buffer_info->byte_stride + 0 * sizeof(float));
                const float p1_y = *reinterpret_cast<const float*>(raw_vertex_ptr + i1 * vertex_buffer_info->byte_stride + 1 * sizeof(float));
                const float p1_z = *reinterpret_cast<const float*>(raw_vertex_ptr + i1 * vertex_buffer_info->byte_stride + 2 * sizeof(float));

                const float p2_x = *reinterpret_cast<const float*>(raw_vertex_ptr + i2 * vertex_buffer_info->byte_stride + 0 * sizeof(float));
                const float p2_y = *reinterpret_cast<const float*>(raw_vertex_ptr + i2 * vertex_buffer_info->byte_stride + 1 * sizeof(float));
                const float p2_z = *reinterpret_cast<const float*>(raw_vertex_ptr + i2 * vertex_buffer_info->byte_stride + 2 * sizeof(float));

                hash_code = erhe::hash::hash(p0_x, p0_y, p0_z, hash_code);
                hash_code = erhe::hash::hash(p1_x, p1_y, p1_z, hash_code);
                hash_code = erhe::hash::hash(p2_x, p2_y, p2_z, hash_code);

                const bvh::v2::Tri<float, 3> triangle{
                    Vec3{p2_x, p2_y, p2_z},
                    Vec3{p1_x, p1_y, p1_z},
                    Vec3{p0_x, p0_y, p0_z}
                };
                tris.emplace_back(triangle);
                bboxes[i] = triangle.get_bbox();
                centers[i] = triangle.get_center();
                m_bbox.include(glm::vec3{p0_x, p0_y, p0_z});
                m_bbox.include(glm::vec3{p1_x, p1_y, p1_z});
                m_bbox.include(glm::vec3{p2_x, p2_y, p2_z});
            }
            log_geometry->trace("BVH hash for {} : {:x}", debug_label(), hash_code);
        }

        Executor_resources& executor_resources = Executor_resources::get_instance();

        const bool prebuilt_ok = prebuilt.has_value() && try_use_prebuilt_bvh(prebuilt.value(), hash_code, triangle_count);
        const bool load_ok     = !prebuilt_ok && load_bvh(m_bvh, hash_code, triangle_count);
        if (prebuilt_ok) {
            m_bvh_source  = Bvh_source::prebuilt;
            m_bvh_quality = prebuilt.value().quality;
        } else if (load_ok) {
            m_bvh_source  = Bvh_source::cache;
            m_bvh_quality = Bvh_build_quality::high; // cache entries are written by the build below
        } else {
            typename bvh::v2::DefaultBuilder<Node>::Config config;
            config.quality = bvh::v2::DefaultBuilder<Node>::Quality::High; // TODO Low
            m_bvh_source  = Bvh_source::built;
            m_bvh_quality = Bvh_build_quality::high;

            {
                ERHE_PROFILE_SCOPE("bvh build");
                erhe::time::Timer timer{m_debug_label.c_str()};

                timer.begin();
                m_bvh = bvh::v2::DefaultBuilder<Node>::build(
                    executor_resources.get_thread_pool(),
                    bboxes,
                    centers,
                    config
                );
                timer.end();

                const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(timer.duration().value()).count();
                log_geometry->info("BVH build {} in {} ms", debug_label(), time);
            }

            const bool save_ok = save_bvh(m_bvh, hash_code);
            if (!save_ok) {
                log_geometry->error("BVH save failed, hash = {}", hash_code);
            }
        }

        // This precomputes some data to speed up traversal further.
        {
            ERHE_PROFILE_SCOPE("bvh precompute");
            m_precomputed_triangles.clear();
            m_precomputed_triangles.resize(tris.size());

            executor_resources.get_executor().for_each(
                0,
                tris.size(),
                [&] (const size_t begin, const size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        auto j = should_permute ? m_bvh.prim_ids[i] : i;
                        m_precomputed_triangles[i] = tris[j];
                    }
                }
            );
        }
        m_triangle_hash  = hash_code;
        m_triangle_count = triangle_count;
    }

    notify_parents_modified();
}

auto Bvh_geometry::try_use_prebuilt_bvh(const Prebuilt_bvh& prebuilt, const uint64_t hash_code, const std::size_t triangle_count) -> bool
{
    ERHE_PROFILE_FUNCTION();

    const std::string reason = validate_prebuilt_bvh(
        prebuilt,
        get_prebuilt_bvh_backend(),
        get_prebuilt_bvh_library(),
        hash_code,
        triangle_count,
        c_traversal_stack_size
    );
    if (!reason.empty()) {
        log_geometry->warn("Prebuilt BVH for {} rejected: {}", debug_label(), reason);
        m_prebuilt_bvh_rejection = reason;
        return false;
    }

    m_bvh.nodes.resize(prebuilt.nodes.size());
    for (std::size_t n = 0, end = prebuilt.nodes.size(); n < end; ++n) {
        const Bvh_node_record& record = prebuilt.nodes[n];
        Node&                  node   = m_bvh.nodes[n];
        for (std::size_t i = 0; i < 6; ++i) {
            node.bounds[i] = record.bounds[i];
        }
        node.index = Node::Index{record.index};
    }
    m_bvh.prim_ids.resize(prebuilt.primitive_ids.size());
    for (std::size_t i = 0, end = prebuilt.primitive_ids.size(); i < end; ++i) {
        m_bvh.prim_ids[i] = prebuilt.primitive_ids[i];
    }
    return true;
}

auto Bvh_geometry::set_prebuilt_bvh(Prebuilt_bvh&& prebuilt) -> bool
{
    m_prebuilt_bvh = std::move(prebuilt);
    return true;
}

auto Bvh_geometry::export_bvh(Prebuilt_bvh& out) const -> bool
{
    if ((m_bvh_source == Bvh_source::none) || !m_triangle_hash.has_value() || m_bvh.nodes.empty()) {
        return false;
    }
    out.backend        = std::string{get_prebuilt_bvh_backend()};
    out.library        = std::string{get_prebuilt_bvh_library()};
    out.quality        = m_bvh_quality;
    out.triangle_hash  = m_triangle_hash.value();
    out.triangle_count = m_triangle_count;
    out.nodes.resize(m_bvh.nodes.size());
    for (std::size_t n = 0, end = m_bvh.nodes.size(); n < end; ++n) {
        const Node&      node   = m_bvh.nodes[n];
        Bvh_node_record& record = out.nodes[n];
        for (std::size_t i = 0; i < 6; ++i) {
            record.bounds[i] = node.bounds[i];
        }
        record.index = static_cast<uint32_t>(node.index.value);
    }
    out.primitive_ids.resize(m_bvh.prim_ids.size());
    for (std::size_t i = 0, end = m_bvh.prim_ids.size(); i < end; ++i) {
        out.primitive_ids[i] = static_cast<uint32_t>(m_bvh.prim_ids[i]);
    }
    return true;
}

auto Bvh_geometry::get_triangle_hash() const -> std::optional<uint64_t>
{
    return m_triangle_hash;
}

auto Bvh_geometry::get_triangle_count() const -> std::size_t
{
    return m_triangle_count;
}

auto Bvh_geometry::get_bvh_source() const -> Bvh_source
{
    return m_bvh_source;
}

auto Bvh_geometry::get_prebuilt_bvh_rejection() const -> std::string_view
{
    return m_prebuilt_bvh_rejection;
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

void Bvh_geometry::set_user_data(const void* ptr)
{
    m_user_data = ptr;
}

auto Bvh_geometry::intersect_instance(Ray& ray, Hit& hit, Bvh_instance* instance) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return false;
    }
    if ((ray.mask & m_mask) == 0) {
        return false;
    }

    const auto transform = (instance != nullptr) ? instance->get_transform() : glm::mat4{1.0};
    bvh::v2::Ray<Scalar, 3> bvh_ray{
        to_bvh(ray.origin),
        to_bvh(ray.direction),
        ray.t_near,
        ray.t_far
    };

    static constexpr size_t invalid_id = std::numeric_limits<size_t>::max();
    static constexpr size_t stack_size = c_traversal_stack_size;
    static constexpr bool   use_robust_traversal = false;

    auto  prim_id = invalid_id;
    float u{0.0f};
    float v{0.0f};

    // Traverse the BVH and get the u, v coordinates of the closest intersection.
    bvh::v2::SmallStack<Bvh::Index, stack_size> stack;
    m_bvh.intersect<false, use_robust_traversal>(
        bvh_ray,
        m_bvh.get_root().index,
        stack,
        [&] (const size_t begin, const size_t end) {
            for (size_t i = begin; i < end; ++i) {
                size_t j = should_permute ? i : m_bvh.prim_ids[i];
                if (auto hit = m_precomputed_triangles[j].intersect(bvh_ray)) {
                    prim_id = i;
                    std::tie(bvh_ray.tmax, u, v) = *hit;
                }
            }
            return prim_id != invalid_id;
        }
    );

    // TODO Would this be ever needed? if (prim_id != invalid_id && (bvh_ray.tmax > 0.0f)) {
    if (prim_id != invalid_id) {
        const auto triangle_index = should_permute ? prim_id : m_bvh.prim_ids[prim_id];
        const auto& triangle = m_precomputed_triangles.at(triangle_index);

        ray.t_far       = bvh_ray.tmax;
        hit.triangle_id = static_cast<unsigned int>(m_bvh.prim_ids[prim_id]);
        hit.uv          = glm::vec2{u, v};
        // Cofactor matrix, not the instance transform: a normal transformed by
        // M is sheared toward the longest axis whenever the scale is
        // non-uniform (a bone proxy, say).
        hit.normal      = glm::vec3{glm::transpose(glm::adjugate(transform)) * glm::vec4{from_bvh(triangle.n), 0.0f}};
        hit.instance    = instance;
        hit.geometry    = this;
        return true;
    }
    return false;
}

/// auto Bvh_geometry::get_sphere() const -> const erhe::math::Sphere&
/// {
///     return m_bounding_sphere;
/// }

auto Bvh_geometry::get_bbox() const -> erhe::math::Aabb
{
    return m_bbox;
}

auto Bvh_geometry::get_mask() const -> uint32_t
{
    return m_mask;
}

auto Bvh_geometry::get_user_data() const -> const void*
{
    return m_user_data;
}

auto Bvh_geometry::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
