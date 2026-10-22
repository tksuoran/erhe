// Light_cluster_binner (doc/plans/clustered_lights.md C2-C4). Deviceless:
// real Light / Camera nodes, Light_set::resolve() and Light_projections::
// apply() without a shadow map. The binner's lists must be conservative for
// the cluster the fragment shader picks, so every check samples positions and
// looks their cluster up with an fp32 copy of erhe_light_cluster_index()
// (res/shaders/erhe_light_cluster.glsl).

#include <gtest/gtest.h>

#include "scene_renderer_test_logging.hpp"

#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/light_cluster.hpp"
#include "erhe_scene_renderer/light_set.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include "erhe_log/log.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/light_snapshot.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <span>
#include <vector>

namespace {

using erhe::scene_renderer::Light_cluster_binner;
using erhe::scene_renderer::Light_cluster_config;
using erhe::scene_renderer::Light_cluster_grid;
using erhe::scene_renderer::Light_cluster_view;
using erhe::scene_renderer::Light_count_limits;
using erhe::scene_renderer::Light_layer_partition;
using erhe::scene_renderer::Light_projections;
using erhe::scene_renderer::Light_set;
using erhe::scene_renderer::Light_shading;

// fp32 copy of erhe_light_cluster_index(): view_relative_position is relative
// to the pass's view origin, pass_view_origin is that origin.
[[nodiscard]] auto cluster_index(
    const Light_cluster_grid& grid,
    const glm::vec3&          pass_view_origin,
    const glm::vec3&          view_relative_position
) -> uint32_t
{
    const glm::vec3  p      = view_relative_position + (pass_view_origin - glm::vec3{grid.view_origin});
    const glm::vec4  clip   = grid.clip_from_cluster_relative * glm::vec4{p, 1.0f};
    const glm::uvec4 counts = grid.counts;
    const glm::vec2  ndc    = (clip.w > 0.0f) ? (glm::vec2{clip} / clip.w) : glm::vec2{0.0f};
    const glm::vec2 counts_xy{static_cast<float>(counts.x), static_cast<float>(counts.y)};
    const glm::vec2 tile_f = glm::clamp(glm::floor(((ndc * 0.5f) + glm::vec2{0.5f}) * counts_xy), glm::vec2{0.0f}, counts_xy - glm::vec2{1.0f});
    const float     depth  = -glm::dot(p, glm::vec3{grid.view_axis});
    const float     slice_f = glm::clamp(
        std::floor((std::log2(std::max(depth, grid.slices.z)) * grid.slices.x) + grid.slices.y),
        0.0f,
        static_cast<float>(counts.z) - 1.0f
    );
    return (((static_cast<uint32_t>(slice_f) * counts.y) + static_cast<uint32_t>(tile_f.y)) * counts.x) + static_cast<uint32_t>(tile_f.x);
}

// The light block conventions of a backend family.
class Backend_conventions
{
public:
    bool                               reverse_depth{true};
    erhe::math::Depth_range            depth_range  {erhe::math::Depth_range::zero_to_one};
    erhe::math::Coordinate_conventions conventions  {};
};

[[nodiscard]] auto vulkan_conventions() -> Backend_conventions
{
    return Backend_conventions{
        .reverse_depth = true,
        .depth_range   = erhe::math::Depth_range::zero_to_one,
        .conventions   = erhe::math::Coordinate_conventions{
            .native_depth_range = erhe::math::Depth_range::zero_to_one,
            .framebuffer_origin = erhe::math::Framebuffer_origin::top_left,
            .texture_origin     = erhe::math::Texture_origin::top_left,
            .clip_space_y_flip  = erhe::math::Clip_space_y_flip::enabled
        }
    };
}

[[nodiscard]] auto opengl_conventions() -> Backend_conventions
{
    return Backend_conventions{
        .reverse_depth = false,
        .depth_range   = erhe::math::Depth_range::negative_one_to_one,
        .conventions   = erhe::math::Coordinate_conventions{}
    };
}

// The four binned categories in slot order (Light_set: type major,
// shadow-mapped first): spot shadow-mapped, spot unshadowed, point
// shadow-mapped, point unshadowed.
class Category_ranges
{
public:
    explicit Category_ranges(const Light_layer_partition& partition)
    {
        begin[0] = partition.per_type_shadow[0] + partition.per_type_nonshadow[0];
        begin[1] = begin[0] + partition.per_type_shadow   [1];
        begin[2] = begin[1] + partition.per_type_nonshadow[1];
        begin[3] = begin[2] + partition.per_type_shadow   [2];
        begin[4] = begin[3] + partition.per_type_nonshadow[2];
    }
    [[nodiscard]] auto get_category(const uint32_t slot) const -> std::size_t
    {
        for (std::size_t category = 0; category < 4; ++category) {
            if (slot < begin[category + 1]) {
                return category;
            }
        }
        return 4;
    }

    std::size_t begin[5]{};
};

[[nodiscard]] auto read_uint(const std::vector<std::byte>& data, const std::size_t uint_index) -> uint32_t
{
    uint32_t value = 0;
    std::memcpy(&value, data.data() + (uint_index * sizeof(uint32_t)), sizeof(uint32_t));
    return value;
}

// One cluster of the light cluster block layout (light_cluster.hpp).
class Cluster_entry
{
public:
    std::size_t           counts[4]{}; // spot shadow-mapped, spot unshadowed, point shadow-mapped, point unshadowed
    std::vector<uint32_t> slots;
};

[[nodiscard]] auto read_cluster(const std::vector<std::byte>& data, const std::size_t cluster) -> Cluster_entry
{
    Cluster_entry entry{};
    const std::size_t header = erhe::scene_renderer::c_light_cluster_header_uint_count * cluster;
    const uint32_t    offset = read_uint(data, header + 0);
    const uint32_t    spot   = read_uint(data, header + 1);
    const uint32_t    point  = read_uint(data, header + 2);
    entry.counts[0] = spot  & 0xffffu;
    entry.counts[1] = spot  >> 16u;
    entry.counts[2] = point & 0xffffu;
    entry.counts[3] = point >> 16u;
    const std::size_t count = entry.counts[0] + entry.counts[1] + entry.counts[2] + entry.counts[3];
    for (std::size_t i = 0; i < count; ++i) {
        const uint32_t packed = read_uint(data, offset + (i >> 1u));
        entry.slots.push_back(((i & 1u) == 0u) ? (packed & 0xffffu) : (packed >> 16u));
    }
    return entry;
}

constexpr std::size_t c_light_array_capacity = 1024;

enum class Cast_shadow : unsigned int
{
    no  = 0,
    yes = 1
};

[[nodiscard]] auto make_scene_light(
    const erhe::scene::Light_type type,
    const glm::vec3&              position,
    const glm::vec3&              target,
    const float                   range,
    const Cast_shadow             cast_shadow
) -> std::shared_ptr<erhe::scene::Light>
{
    std::shared_ptr<erhe::scene::Light> light = std::make_shared<erhe::scene::Light>("cluster test light");
    light->set_light_type      (type);
    light->set_color           (glm::vec3{1.0f, 1.0f, 1.0f});
    light->set_intensity       (1.0f);
    light->set_range           (range);
    light->set_inner_spot_angle(0.2f * glm::pi<float>());
    light->set_outer_spot_angle(0.3f * glm::pi<float>());
    light->set_cast_shadow     (cast_shadow == Cast_shadow::yes);
    const glm::vec3 direction = target - position;
    const glm::vec3 up        = (std::abs(direction.y) > (0.9f * glm::length(direction))) ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    light->set_world_from_node(erhe::math::create_look_at(position, target, up));
    return light;
}

// A scene of lights resolved and applied for one camera, as the editor's
// Shadow_render_node does it in clustered mode (Light_count_limits from
// get_light_count_limits() with Light_shading_mode::clustered).
class Light_cluster_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Deviceless binary: no GPU test environment creates the log sinks.
        static std::once_flag s_once;
        std::call_once(s_once, []() { erhe::log::initialize_log_sinks(); });
        erhe::scene_renderer::test::initialize_scene_renderer_test_logging();
    }

    void make_camera(const erhe::scene::Projection::Type projection_type)
    {
        m_camera = std::make_shared<erhe::scene::Camera>("cluster test camera");
        erhe::scene::Projection projection{};
        projection.projection_type     = projection_type;
        projection.perspective_z_near  = 0.1f;
        projection.perspective_z_far   = 100.0f;
        projection.fov_y               = glm::radians(60.0f);
        projection.orthographic_z_near = 0.1f;
        projection.orthographic_z_far  = 100.0f;
        projection.ortho_height        = 12.0f;
        // Asymmetric, as an XR eye's field of view (perspective_xr).
        projection.fov_left            = -0.9f;
        projection.fov_right           =  0.4f;
        projection.fov_up              =  0.5f;
        projection.fov_down            = -0.8f;
        m_camera->set_projection(projection);
        // Not axis aligned: the grid's view axis and clip matrix are general.
        m_camera->set_world_from_node(
            erhe::math::create_look_at(glm::vec3{3.0f, 2.0f, 5.0f}, glm::vec3{-1.0f, 0.5f, -6.0f}, glm::vec3{0.0f, 1.0f, 0.0f})
        );
        m_camera_snapshot.capture(*m_camera);
    }

    // count spot + count point lights at random positions around the camera
    // (in front, beside, behind and past the far end of the grid), the first
    // few of each type shadow-casting, plus two directional lights and one
    // unbounded (range 0) spot light.
    void make_lights(const std::size_t count, const float min_range, const float max_range)
    {
        std::mt19937 random{12345u};
        std::uniform_real_distribution<float> x_distribution{-20.0f, 20.0f};
        std::uniform_real_distribution<float> y_distribution{ -6.0f,  8.0f};
        std::uniform_real_distribution<float> z_distribution{-40.0f, 12.0f};
        std::uniform_real_distribution<float> range_distribution{min_range, max_range};
        m_lights.clear();
        m_lights.push_back(make_scene_light(erhe::scene::Light_type::directional, glm::vec3{0.0f, 10.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.5f}, 0.0f, Cast_shadow::yes));
        m_lights.push_back(make_scene_light(erhe::scene::Light_type::directional, glm::vec3{0.0f, 10.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f}, 0.0f, Cast_shadow::no));
        m_lights.push_back(make_scene_light(erhe::scene::Light_type::spot, glm::vec3{0.0f, 4.0f, -3.0f}, glm::vec3{0.0f, 0.0f, -3.0f}, 0.0f, Cast_shadow::no));
        for (std::size_t i = 0; i < count; ++i) {
            for (const erhe::scene::Light_type type : {erhe::scene::Light_type::spot, erhe::scene::Light_type::point}) {
                const glm::vec3 position{x_distribution(random), y_distribution(random), z_distribution(random)};
                const glm::vec3 target  {x_distribution(random), y_distribution(random), z_distribution(random)};
                const Cast_shadow cast_shadow = (i < 3) ? Cast_shadow::yes : Cast_shadow::no;
                m_lights.push_back(make_scene_light(type, position, target, range_distribution(random), cast_shadow));
            }
        }
    }

    void resolve_and_apply(const Backend_conventions& backend)
    {
        Light_count_limits limits{};
        limits.per_type_shadow[0] = 1;
        limits.per_type_shadow[1] = 2;
        limits.per_type_shadow[2] = 2;
        limits.max_light_count    = 1024;
        limits.light_shading      = Light_shading::clustered;
        for (std::size_t t = 0; t < 3; ++t) {
            limits.per_type_unshadowed[t] = limits.max_light_count;
        }
        m_light_set.invalidate();
        static_cast<void>(m_light_set.resolve(std::span<const std::shared_ptr<erhe::scene::Light>>{m_lights}, limits, c_light_array_capacity));
        m_light_projections.apply(
            m_light_set,
            &m_camera_snapshot,
            m_viewport,
            erhe::math::Viewport{0, 0, 1024, 1024},
            std::shared_ptr<erhe::graphics::Texture>{},
            backend.reverse_depth,
            backend.depth_range,
            backend.conventions
        );
        m_backend = backend;
    }

    void bin(const Light_cluster_config& config)
    {
        m_binner.bin(
            m_light_projections,
            Light_cluster_view{
                .camera        = &m_camera_snapshot,
                .viewport      = m_viewport,
                .reverse_depth = m_backend.reverse_depth,
                .depth_range   = m_backend.depth_range,
                .conventions   = m_backend.conventions
            },
            config
        );
        m_data.assign(m_binner.get_byte_count(), std::byte{0});
        m_binner.write(std::span<std::byte>{m_data});
    }

    [[nodiscard]] auto get_cluster_at(const glm::vec3& world_position) const -> uint32_t
    {
        const glm::vec3 pass_view_origin = erhe::scene_renderer::get_view_origin(m_camera_snapshot.world_from_node());
        return cluster_index(m_binner.get_grid(), pass_view_origin, world_position - pass_view_origin);
    }

    [[nodiscard]] auto get_light_position(const std::size_t slot) const -> glm::dvec3
    {
        // As Light_buffer writes position_and_inner_spot_cos.
        const erhe::scene::Light_projection_transforms& transforms = m_light_projections.light_projection_transforms[slot];
        return glm::dvec3{glm::vec3{transforms.world_from_light_camera.get_matrix() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}}};
    }

    // Whether the fp32 get_spot_attenuation() of standard.frag is nonzero at
    // world_position (always for point lights and unbounded spot lights,
    // which the binner lists without a cone test).
    [[nodiscard]] auto spot_attenuation_nonzero(const std::size_t slot, const glm::vec3& world_position) const -> bool
    {
        const erhe::scene::Light_snapshot& light = *m_light_projections.light_projection_transforms[slot].light;
        if ((light.get_light_type() != erhe::scene::Light_type::spot) || (light.get_range() <= 0.0f)) {
            return true;
        }
        // As Light_buffer writes direction_and_outer_spot_cos and as the
        // shader evaluates it.
        const float     outer_cos      = std::cos(light.get_outer_spot_angle() * 0.5f);
        const glm::vec3 spot_direction = glm::normalize(light.get_light_frame().direction);
        const glm::vec3 point_to_light = glm::vec3{get_light_position(slot)} - world_position;
        // The host compiler may contract the normalize / dot multiply-adds
        // here (this file is not built with -ffp-contract=off). That is
        // harmless: the binner's cone is widened by 2^-16 in cosine space,
        // far beyond the ulps that contraction changes, so this models the
        // shader within the margin either way.
        const float     actual_cos     = glm::dot(glm::normalize(spot_direction), glm::normalize(point_to_light));
        return actual_cos > outer_cos;
    }

    // Every cull of the cone test, checked directly: for every closed
    // cluster (interior tile, not slice 0) and every bounded spot light
    // whose range sphere reaches the cluster's widened bounds but which the
    // cluster does not list, no point of the widened bounds (corners and a
    // 9 x 9 grid on each face) has nonzero fp32 spot attenuation.
    void check_cone_culls()
    {
        ASSERT_EQ(m_binner.get_overflow_cluster_count(), 0u) << "a capacity cut is not a cull";
        std::vector<std::byte> inputs(m_binner.get_input_byte_count());
        m_binner.write_inputs(std::span<std::byte>{inputs});
        const auto read_vec4 = [&inputs](const std::size_t index) -> glm::vec4 {
            glm::vec4 value{};
            std::memcpy(&value, inputs.data() + (erhe::scene_renderer::c_light_cluster_bin_header_uvec4_count * sizeof(glm::uvec4)) + (index * sizeof(glm::vec4)), sizeof(value));
            return value;
        };
        const glm::uvec4      counts      = m_binner.get_grid().counts;
        const uint32_t        tiles_x     = counts.x;
        const uint32_t        tiles_y     = counts.y;
        const uint32_t        slice_count = counts.z;
        const std::size_t     light_base  = slice_count + (2 * (static_cast<std::size_t>(tiles_x) + 1) * (static_cast<std::size_t>(tiles_y) + 1));
        const Category_ranges ranges{m_light_projections.light_partition};
        const glm::dmat4      world_from_node{m_camera_snapshot.world_from_node()};
        std::size_t culls  = 0;
        std::size_t points = 0;
        for (uint32_t k = 1; k < slice_count; ++k) {
            const glm::vec4 slice = read_vec4(k);
            for (uint32_t iy = 1; (iy + 1) < tiles_y; ++iy) {
                for (uint32_t ix = 1; (ix + 1) < tiles_x; ++ix) {
                    glm::vec3 lo{0.0f};
                    glm::vec3 hi{0.0f};
                    for (uint32_t c = 0; c < 4; ++c) {
                        const std::size_t corner    = ((static_cast<std::size_t>(iy + (c >> 1u)) * (tiles_x + 1)) + ix + (c & 1u));
                        const glm::vec3   origin    {read_vec4(slice_count + (2 * corner) + 0)};
                        const glm::vec3   direction {read_vec4(slice_count + (2 * corner) + 1)};
                        const glm::vec3   p0        = origin + (slice.x * direction);
                        const glm::vec3   p1        = origin + (slice.y * direction);
                        lo = (c == 0) ? glm::min(p0, p1) : glm::min(lo, glm::min(p0, p1));
                        hi = (c == 0) ? glm::max(p0, p1) : glm::max(hi, glm::max(p0, p1));
                    }
                    lo -= glm::vec3{slice.z};
                    hi += glm::vec3{slice.z};
                    const std::size_t               cluster = (((static_cast<std::size_t>(k) * tiles_y) + iy) * tiles_x) + ix;
                    const std::span<const uint32_t> slots   = m_binner.get_cluster_slots(cluster);
                    for (std::size_t slot = ranges.begin[0]; slot < ranges.begin[2]; ++slot) {
                        const glm::vec4 light = read_vec4(light_base + (slot - ranges.begin[0]));
                        if (light.w < 0.0f) {
                            continue;
                        }
                        const glm::vec3 offset   = glm::vec3{light} - glm::clamp(glm::vec3{light}, lo, hi);
                        const bool      in_range = glm::dot(offset, offset) <= (light.w * light.w);
                        if (!in_range || std::binary_search(slots.begin(), slots.end(), static_cast<uint32_t>(slot))) {
                            continue;
                        }
                        ++culls;
                        bool lit = false;
                        for (int axis = 0; (axis < 3) && !lit; ++axis) {
                            for (int side = 0; (side < 2) && !lit; ++side) {
                                for (int i = 0; (i < 9) && !lit; ++i) {
                                    for (int j = 0; (j < 9) && !lit; ++j) {
                                        const glm::dvec3 l{lo};
                                        const glm::dvec3 h{hi};
                                        glm::dvec3 t{0.0};
                                        t[axis]           = static_cast<double>(side);
                                        t[(axis + 1) % 3] = static_cast<double>(i) / 8.0;
                                        t[(axis + 2) % 3] = static_cast<double>(j) / 8.0;
                                        const glm::dvec3 node_position = l + (t * (h - l)); // i, j in {0, 8} are the corners
                                        const glm::vec3  world_position{world_from_node * glm::dvec4{node_position, 1.0}};
                                        ++points;
                                        lit = spot_attenuation_nonzero(slot, world_position);
                                        EXPECT_FALSE(lit)
                                            << "cluster " << cluster << " culls spot slot " << slot << " but its bounds point ("
                                            << world_position.x << ", " << world_position.y << ", " << world_position.z << ") is lit";
                                    }
                                }
                            }
                        }
                        if (lit && (++m_failures >= 20)) {
                            return;
                        }
                    }
                }
            }
        }
        EXPECT_GT(culls, 100u) << "the cone test must cull";
        static_cast<void>(points);
    }

    // Every binned light reaching world_position (distance below its range,
    // or no range at all, and inside a spot light's cone) must be in the list
    // of the cluster the shader picks for it.
    void check_position(const glm::vec3& world_position)
    {
        if (m_failures >= 20) {
            return; // enough reported
        }
        const glm::vec3        pass_view_origin = erhe::scene_renderer::get_view_origin(m_camera_snapshot.world_from_node());
        const glm::vec3        view_relative    = world_position - pass_view_origin;
        const uint32_t         cluster          = cluster_index(m_binner.get_grid(), pass_view_origin, view_relative);
        ASSERT_LT(cluster, m_binner.get_grid().get_cluster_count());
        const std::span<const uint32_t> slots   = m_binner.get_cluster_slots(cluster);
        const Category_ranges  ranges{m_light_projections.light_partition};
        for (std::size_t slot = ranges.begin[0]; slot < ranges.begin[4]; ++slot) {
            const erhe::scene::Light_snapshot& light = *m_light_projections.light_projection_transforms[slot].light;
            const double range = static_cast<double>(light.get_range());
            const bool reaches =
                ((range <= 0.0) || (glm::distance(glm::dvec3{world_position}, get_light_position(slot)) < range)) &&
                spot_attenuation_nonzero(slot, world_position);
            if (!reaches) {
                continue;
            }
            ++m_reaching_checks;
            const bool listed = std::binary_search(slots.begin(), slots.end(), static_cast<uint32_t>(slot));
            EXPECT_TRUE(listed)
                << "slot " << slot << " reaches (" << world_position.x << ", " << world_position.y << ", " << world_position.z
                << ") but cluster " << cluster << " does not list it";
            if (!listed) {
                ++m_failures;
            }
        }
    }

    // Positions in a box around the camera (inside, beside, behind and past
    // the far end of the cluster frustum) plus positions inside every light's
    // range sphere.
    void check_sampled_positions()
    {
        std::mt19937 random{777u};
        std::uniform_real_distribution<float> x_distribution{-35.0f, 35.0f};
        std::uniform_real_distribution<float> y_distribution{-15.0f, 18.0f};
        std::uniform_real_distribution<float> z_distribution{-70.0f, 25.0f};
        for (std::size_t i = 0; i < 20000; ++i) {
            check_position(glm::vec3{x_distribution(random), y_distribution(random), z_distribution(random)});
        }
        std::uniform_real_distribution<float> unit{-1.0f, 1.0f};
        const Category_ranges ranges{m_light_projections.light_partition};
        for (std::size_t slot = ranges.begin[0]; slot < ranges.begin[4]; ++slot) {
            const float range = m_light_projections.light_projection_transforms[slot].light->get_range();
            if (range <= 0.0f) {
                continue;
            }
            const glm::vec3 centre{get_light_position(slot)};
            for (std::size_t i = 0; i < 400; ++i) {
                const glm::vec3 offset{unit(random), unit(random), unit(random)};
                if (glm::dot(offset, offset) > 1.0f) {
                    continue;
                }
                check_position(centre + (range * offset));
            }
        }
        EXPECT_GT(m_reaching_checks, 1000u) << "the samples must exercise the lists";
    }

    // The written block: every cluster's list is the binner's, ascending,
    // only spot / point slots, category counts matching its slots.
    void check_layout()
    {
        const Category_ranges ranges{m_light_projections.light_partition};
        const std::size_t cluster_count = m_binner.get_grid().get_cluster_count();
        for (std::size_t cluster = 0; cluster < cluster_count; ++cluster) {
            const Cluster_entry entry = read_cluster(m_data, cluster);
            const std::span<const uint32_t> slots = m_binner.get_cluster_slots(cluster);
            ASSERT_EQ(entry.slots.size(), slots.size()) << "cluster " << cluster;
            std::size_t counts[4]{};
            for (std::size_t i = 0; i < slots.size(); ++i) {
                ASSERT_EQ(entry.slots[i], slots[i]) << "cluster " << cluster;
                if (i > 0) {
                    ASSERT_LT(slots[i - 1], slots[i]) << "cluster " << cluster << " list is not ascending";
                }
                ASSERT_GE(slots[i], ranges.begin[0]) << "a directional light is binned";
                ASSERT_LT(slots[i], ranges.begin[4]);
                ++counts[ranges.get_category(slots[i])];
            }
            for (std::size_t category = 0; category < 4; ++category) {
                ASSERT_EQ(entry.counts[category], counts[category]) << "cluster " << cluster << " category " << category;
            }
        }
    }

    std::shared_ptr<erhe::scene::Camera>             m_camera;
    erhe::scene::Camera_snapshot                     m_camera_snapshot;
    erhe::math::Viewport                             m_viewport{0, 0, 1334, 1120};
    std::vector<std::shared_ptr<erhe::scene::Light>> m_lights;
    Light_set                                        m_light_set;
    Light_projections                                m_light_projections;
    Backend_conventions                              m_backend{};
    Light_cluster_binner                             m_binner;
    std::vector<std::byte>                           m_data;
    std::size_t                                      m_reaching_checks{0};
    std::size_t                                      m_failures{0};
};

} // anonymous namespace

TEST_F(Light_cluster_test, perspective_vulkan_lists_are_conservative)
{
    make_camera(erhe::scene::Projection::Type::perspective_vertical);
    make_lights(24, 0.5f, 8.0f);
    resolve_and_apply(vulkan_conventions());
    bin(Light_cluster_config{});
    const Light_cluster_grid& grid = m_binner.get_grid();
    EXPECT_EQ(grid.counts.x, 21u);
    EXPECT_EQ(grid.counts.y, 18u);
    EXPECT_EQ(grid.counts.z, 24u);
    EXPECT_EQ(m_binner.get_overflow_cluster_count(), 0u);
    check_layout();
    check_sampled_positions();

    // Tight, not only conservative: no cluster lists every binned light.
    const Category_ranges ranges{m_light_projections.light_partition};
    EXPECT_LT(m_binner.get_max_list_length(), ranges.begin[4] - ranges.begin[0]);
}

TEST_F(Light_cluster_test, far_cluster_does_not_list_small_light)
{
    make_camera(erhe::scene::Projection::Type::perspective_vertical);
    make_lights(24, 0.5f, 8.0f);
    // A small light 10 m in front of the camera, on its axis.
    const glm::vec3 eye    {3.0f, 2.0f, 5.0f};
    const glm::vec3 forward = glm::normalize(glm::vec3{-1.0f, 0.5f, -6.0f} - eye);
    const glm::vec3 small_light_position = eye + (10.0f * forward);
    m_lights.push_back(make_scene_light(erhe::scene::Light_type::point, small_light_position, small_light_position + forward, 0.5f, Cast_shadow::no));
    resolve_and_apply(vulkan_conventions());
    bin(Light_cluster_config{});

    const std::size_t slot = m_light_set.find_slot(m_lights.back()->get_id());
    ASSERT_NE(slot, SIZE_MAX);
    const std::span<const uint32_t> near_slots = m_binner.get_cluster_slots(get_cluster_at(small_light_position));
    EXPECT_TRUE(std::binary_search(near_slots.begin(), near_slots.end(), static_cast<uint32_t>(slot)));
    // 30 m in front of the camera: a different, far slice.
    const std::span<const uint32_t> far_slots = m_binner.get_cluster_slots(get_cluster_at(eye + (30.0f * forward)));
    EXPECT_FALSE(std::binary_search(far_slots.begin(), far_slots.end(), static_cast<uint32_t>(slot)));
}

TEST_F(Light_cluster_test, asymmetric_perspective_lists_are_conservative)
{
    make_camera(erhe::scene::Projection::Type::perspective_xr);
    make_lights(24, 0.5f, 8.0f);
    resolve_and_apply(vulkan_conventions());
    bin(Light_cluster_config{});
    check_layout();
    check_sampled_positions();
}

TEST_F(Light_cluster_test, shaded_total_stops_at_light_array_capacity)
{
    make_camera(erhe::scene::Projection::Type::perspective_vertical);
    make_lights(24, 0.5f, 8.0f);
    Light_count_limits limits{};
    limits.per_type_shadow[1] = 2;
    limits.light_shading      = Light_shading::clustered;
    for (std::size_t t = 0; t < 3; ++t) {
        limits.per_type_unshadowed[t] = 1000;
    }
    // max_light_count left unbounded: the capacity alone bounds the total.
    constexpr std::size_t capacity = 10;
    static_cast<void>(m_light_set.resolve(std::span<const std::shared_ptr<erhe::scene::Light>>{m_lights}, limits, capacity));
    EXPECT_EQ(m_light_set.get_lights().size(), capacity);
    EXPECT_EQ(m_light_set.get_light_count_limits().max_light_count, capacity);
    const Light_layer_partition partition = erhe::scene_renderer::compute_light_layer_partition(
        std::span<const std::shared_ptr<erhe::scene::Light>>{m_lights}, limits, capacity
    );
    EXPECT_EQ(partition, m_light_set.get_partition());
    EXPECT_LE(erhe::scene_renderer::get_cluster_slot_end(partition), capacity);
}

TEST_F(Light_cluster_test, perspective_opengl_lists_are_conservative)
{
    make_camera(erhe::scene::Projection::Type::perspective_vertical);
    make_lights(24, 0.5f, 8.0f);
    resolve_and_apply(opengl_conventions());
    bin(Light_cluster_config{.tile_size = 37, .slice_count = 13, .capacity = 64});
    check_layout();
    check_sampled_positions();
}

TEST_F(Light_cluster_test, orthographic_lists_are_conservative)
{
    make_camera(erhe::scene::Projection::Type::orthographic_vertical);
    make_lights(24, 0.5f, 8.0f);
    resolve_and_apply(vulkan_conventions());
    bin(Light_cluster_config{});
    check_layout();
    check_sampled_positions();
}

TEST_F(Light_cluster_test, partition_and_unbounded_lights)
{
    make_camera(erhe::scene::Projection::Type::perspective_vertical);
    make_lights(24, 0.5f, 8.0f);
    resolve_and_apply(vulkan_conventions());

    // Clustered: every active light is shaded; the shadow limits pick the
    // shadow-mapped ones.
    const Light_layer_partition& partition = m_light_projections.light_partition;
    EXPECT_EQ(partition.light_shading, Light_shading::clustered);
    EXPECT_EQ(partition.per_type_shadow   [0], 1u);
    EXPECT_EQ(partition.per_type_nonshadow[0], 1u);
    EXPECT_EQ(partition.per_type_shadow   [1], 2u);
    EXPECT_EQ(partition.per_type_nonshadow[1], 23u); // 22 random + the unbounded one
    EXPECT_EQ(partition.per_type_shadow   [2], 2u);
    EXPECT_EQ(partition.per_type_nonshadow[2], 22u);

    bin(Light_cluster_config{});
    // The unbounded spot light (range 0) is in every cluster.
    const std::size_t unbounded_slot = m_light_set.find_slot(m_lights[2]->get_id());
    ASSERT_NE(unbounded_slot, SIZE_MAX);
    for (std::size_t cluster = 0, end = m_binner.get_grid().get_cluster_count(); cluster < end; ++cluster) {
        const std::span<const uint32_t> slots = m_binner.get_cluster_slots(cluster);
        ASSERT_TRUE(std::binary_search(slots.begin(), slots.end(), static_cast<uint32_t>(unbounded_slot))) << "cluster " << cluster;
    }
}

TEST_F(Light_cluster_test, overflow_keeps_lowest_slots)
{
    make_camera(erhe::scene::Projection::Type::perspective_vertical);
    // Large ranges: many lights per cluster.
    make_lights(24, 6.0f, 20.0f);
    resolve_and_apply(vulkan_conventions());

    bin(Light_cluster_config{.tile_size = 64, .slice_count = 24, .capacity = 1024});
    ASSERT_EQ(m_binner.get_overflow_cluster_count(), 0u);
    const std::size_t cluster_count   = m_binner.get_grid().get_cluster_count();
    const std::size_t max_list_length = m_binner.get_max_list_length();
    std::vector<std::vector<uint32_t>> full_lists;
    std::size_t expected_overflow = 0;
    constexpr std::size_t capacity = 5;
    for (std::size_t cluster = 0; cluster < cluster_count; ++cluster) {
        const std::span<const uint32_t> slots = m_binner.get_cluster_slots(cluster);
        full_lists.emplace_back(slots.begin(), slots.end());
        if (slots.size() > capacity) {
            ++expected_overflow;
        }
    }
    ASSERT_GT(expected_overflow, 0u) << "the scene must overflow the test capacity";

    bin(Light_cluster_config{.tile_size = 64, .slice_count = 24, .capacity = static_cast<int>(capacity)});
    EXPECT_EQ(m_binner.get_overflow_cluster_count(), expected_overflow);
    EXPECT_EQ(m_binner.get_max_list_length(), max_list_length) << "the longest list is reported before the cut";
    for (std::size_t cluster = 0; cluster < cluster_count; ++cluster) {
        const std::span<const uint32_t> slots = m_binner.get_cluster_slots(cluster);
        const std::vector<uint32_t>&    full  = full_lists[cluster];
        ASSERT_EQ(slots.size(), std::min(full.size(), capacity)) << "cluster " << cluster;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            ASSERT_EQ(slots[i], full[i]) << "cluster " << cluster << " does not keep its lowest slots";
        }
    }
    check_layout();
}

TEST_F(Light_cluster_test, spot_cone_lists_are_conservative)
{
    // Spot lights from a hair-thin cone to wider than a hemisphere; positions
    // inside each cone just within its outer angle, from next to the apex to
    // the end of the range: every one reaching a position with nonzero spot
    // attenuation is in that position's cluster.
    make_camera(erhe::scene::Projection::Type::perspective_vertical);
    make_lights(4, 0.5f, 8.0f);
    std::mt19937 random{4711u};
    std::uniform_real_distribution<float> x_distribution{-12.0f, 12.0f};
    std::uniform_real_distribution<float> y_distribution{ -4.0f,  6.0f};
    std::uniform_real_distribution<float> z_distribution{-30.0f,  8.0f};
    std::uniform_real_distribution<float> range_distribution{1.0f, 12.0f};
    for (const float outer_degrees : {0.5f, 2.0f, 10.0f, 30.0f, 60.0f, 90.0f, 120.0f, 170.0f, 179.0f, 179.9f, 180.0f, 180.1f, 181.0f, 200.0f, 270.0f, 359.0f}) {
        for (std::size_t i = 0; i < 3; ++i) {
            const glm::vec3 position{x_distribution(random), y_distribution(random), z_distribution(random)};
            const glm::vec3 target  {x_distribution(random), y_distribution(random), z_distribution(random)};
            std::shared_ptr<erhe::scene::Light> light = make_scene_light(erhe::scene::Light_type::spot, position, target, range_distribution(random), Cast_shadow::no);
            light->set_inner_spot_angle(0.8f * glm::radians(outer_degrees));
            light->set_outer_spot_angle(glm::radians(outer_degrees));
            m_lights.push_back(light);
        }
    }
    resolve_and_apply(vulkan_conventions());
    bin(Light_cluster_config{});
    check_layout();

    std::uniform_real_distribution<double> azimuth_distribution{0.0, 2.0 * glm::pi<double>()};
    const Category_ranges ranges{m_light_projections.light_partition};
    for (std::size_t slot = ranges.begin[0]; slot < ranges.begin[2]; ++slot) {
        const erhe::scene::Light_snapshot& light = *m_light_projections.light_projection_transforms[slot].light;
        const double range = static_cast<double>(light.get_range());
        if (range <= 0.0) {
            continue;
        }
        const glm::dvec3 apex       = get_light_position(slot);
        const glm::dvec3 axis       = -glm::normalize(glm::dvec3{light.get_light_frame().direction});
        const glm::dvec3 helper     = (std::abs(axis.y) < 0.9) ? glm::dvec3{0.0, 1.0, 0.0} : glm::dvec3{1.0, 0.0, 0.0};
        const glm::dvec3 u          = glm::normalize(glm::cross(axis, helper));
        const glm::dvec3 v          = glm::cross(axis, u);
        const double     half_angle = std::min(0.5 * static_cast<double>(light.get_outer_spot_angle()), glm::pi<double>());
        for (const double angle_fraction : {0.0, 0.5, 0.9, 0.99, 0.9999, 0.999999}) {
            for (const double distance_fraction : {1.0e-4, 1.0e-3, 1.0e-2, 0.1, 0.5, 0.9, 0.999}) {
                for (std::size_t i = 0; i < 4; ++i) {
                    const double     theta     = angle_fraction * half_angle;
                    const double     phi       = azimuth_distribution(random);
                    const glm::dvec3 direction = (std::cos(theta) * axis) + (std::sin(theta) * ((std::cos(phi) * u) + (std::sin(phi) * v)));
                    check_position(glm::vec3{apex + ((distance_fraction * range) * direction)});
                }
            }
        }
    }
    check_sampled_positions();
    check_cone_culls();
}

TEST_F(Light_cluster_test, spot_cone_culls_clusters_behind_and_beside)
{
    // Spot lights 10 m in front of the camera on its axis, pointing away
    // from it, with a range reaching back past the camera: the clusters
    // behind the apex and beside a narrow cone do not list them, the
    // clusters inside the cone do.
    make_camera(erhe::scene::Projection::Type::perspective_vertical);
    const glm::vec3 eye    {3.0f, 2.0f, 5.0f};
    const glm::vec3 forward = glm::normalize(glm::vec3{-1.0f, 0.5f, -6.0f} - eye);
    const glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}));
    const glm::vec3 apex    = eye + (10.0f * forward);
    m_lights.clear();
    const auto add_spot = [&](const float outer_degrees) -> std::shared_ptr<erhe::scene::Light> {
        std::shared_ptr<erhe::scene::Light> light = make_scene_light(erhe::scene::Light_type::spot, apex, apex + forward, 20.0f, Cast_shadow::no);
        light->set_inner_spot_angle(0.5f * glm::radians(outer_degrees));
        light->set_outer_spot_angle(glm::radians(outer_degrees));
        m_lights.push_back(light);
        return light;
    };
    const std::shared_ptr<erhe::scene::Light> narrow     = add_spot( 10.0f); // half angle 5 degrees
    const std::shared_ptr<erhe::scene::Light> wide       = add_spot(170.0f); // half angle 85 degrees
    const std::shared_ptr<erhe::scene::Light> very_wide  = add_spot(240.0f); // half angle 120 degrees
    resolve_and_apply(vulkan_conventions());
    bin(Light_cluster_config{});
    check_layout();

    const auto is_listed = [&](const std::shared_ptr<erhe::scene::Light>& light, const glm::vec3& position) -> bool {
        const std::size_t slot = m_light_set.find_slot(light->get_id());
        EXPECT_NE(slot, SIZE_MAX);
        const uint32_t cluster = get_cluster_at(position);
        EXPECT_NE(m_binner.get_grid().counts.z, 1u);
        const std::span<const uint32_t> slots = m_binner.get_cluster_slots(cluster);
        return std::binary_search(slots.begin(), slots.end(), static_cast<uint32_t>(slot));
    };
    const glm::vec3 inside = eye + (15.0f * forward);
    const glm::vec3 behind = eye + ( 5.0f * forward);                   // in range, behind the apex
    const glm::vec3 beside = eye + (15.0f * forward) + (6.0f * right);  // in range, 50 degrees off the axis
    const glm::vec3 side   = apex + (6.0f * right) + (0.5f * forward);  // 85 degrees off the axis
    for (const std::shared_ptr<erhe::scene::Light>& light : {narrow, wide, very_wide}) {
        EXPECT_TRUE(is_listed(light, inside)) << light->get_outer_spot_angle();
    }
    EXPECT_FALSE(is_listed(narrow,    behind));
    EXPECT_FALSE(is_listed(narrow,    beside));
    EXPECT_FALSE(is_listed(wide,      behind));
    EXPECT_TRUE (is_listed(wide,      beside));
    EXPECT_FALSE(is_listed(very_wide, behind)); // 180 degrees off the axis, outside the 120 degree half angle
    EXPECT_TRUE (is_listed(very_wide, beside));
    EXPECT_TRUE (is_listed(very_wide, side));

    // The listed positions are reached with nonzero spot attenuation; the
    // culled ones are not (the conservative direction is checked by
    // spot_cone_lists_are_conservative).
    check_position(inside);
    check_position(beside);
    check_position(side);
}

TEST_F(Light_cluster_test, single_cluster_grid)
{
    make_camera(erhe::scene::Projection::Type::perspective_vertical);
    make_lights(5, 0.5f, 8.0f);
    resolve_and_apply(vulkan_conventions());

    const Light_layer_partition& partition = m_light_projections.light_partition;
    const Category_ranges        ranges{partition};
    const std::size_t            byte_count = erhe::scene_renderer::get_single_cluster_byte_count(partition);
    const std::size_t            slot_count = ranges.begin[4] - ranges.begin[0];
    EXPECT_EQ(byte_count, (erhe::scene_renderer::c_light_cluster_header_uint_count + ((slot_count + 1) / 2)) * sizeof(uint32_t));
    m_data.assign(byte_count, std::byte{0});
    erhe::scene_renderer::write_single_cluster(partition, std::span<std::byte>{m_data});
    const Cluster_entry entry = read_cluster(m_data, 0);
    EXPECT_EQ(entry.counts[0], partition.per_type_shadow   [1]);
    EXPECT_EQ(entry.counts[1], partition.per_type_nonshadow[1]);
    EXPECT_EQ(entry.counts[2], partition.per_type_shadow   [2]);
    EXPECT_EQ(entry.counts[3], partition.per_type_nonshadow[2]);
    ASSERT_EQ(entry.slots.size(), slot_count);
    for (std::size_t i = 0; i < slot_count; ++i) {
        EXPECT_EQ(entry.slots[i], ranges.begin[0] + i);
    }

    // The default grid (what update() writes for an unbinned pass) puts
    // every position in cluster 0, beside and behind the camera included.
    const Light_cluster_grid grid{};
    EXPECT_EQ(grid.get_cluster_count(), 1u);
    const glm::vec3 origin{1.0f, 2.0f, 3.0f};
    for (const glm::vec3 position : {glm::vec3{0.0f}, glm::vec3{100.0f, -50.0f, 7.0f}, glm::vec3{-3.0f, 4.0f, -1000.0f}, glm::vec3{0.0f, 0.0f, 1000.0f}}) {
        EXPECT_EQ(cluster_index(grid, origin, position), 0u);
    }
}
