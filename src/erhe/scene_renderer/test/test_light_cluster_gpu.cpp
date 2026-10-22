// Light_cluster_gpu_binner (doc/plans/clustered_lights.md C4, V2): the
// compute binner's lists equal the CPU binner's lists of the same inputs,
// cluster by cluster, and its overflow counters equal the CPU's. A synthetic
// light set (spot and point lights in front of, beside, behind and past the
// far end of the camera, shadow-mapped and unshadowed, one unbounded spot
// light, two directional lights) is resolved and projected without a shadow
// map, as the deviceless Light_cluster_test does, then binned both ways.

#include "gpu_test_fixture.hpp"
#include "scene_renderer_test_logging.hpp"

#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/light_cluster.hpp"
#include "erhe_scene_renderer/light_cluster_gpu_binner.hpp"
#include "erhe_scene_renderer/light_set.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene/light.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace erhe::scene_renderer::test {

using erhe::graphics::test::Gpu_test;

namespace {

[[nodiscard]] auto shader_path() -> std::filesystem::path
{
    return std::filesystem::path{ERHE_SCENE_RENDERER_TEST_REPO_ROOT} / "res" / "shaders";
}

[[nodiscard]] auto read_uint(const std::vector<std::byte>& data, const std::size_t uint_index) -> uint32_t
{
    uint32_t value = 0;
    std::memcpy(&value, data.data() + (uint_index * sizeof(uint32_t)), sizeof(uint32_t));
    return value;
}

[[nodiscard]] auto make_scene_light(
    const erhe::scene::Light_type type,
    const glm::vec3&              position,
    const glm::vec3&              target,
    const float                   range,
    const bool                    cast_shadow
) -> std::shared_ptr<erhe::scene::Light>
{
    std::shared_ptr<erhe::scene::Light> light = std::make_shared<erhe::scene::Light>("gpu cluster test light");
    light->set_light_type      (type);
    light->set_color           (glm::vec3{1.0f, 1.0f, 1.0f});
    light->set_intensity       (1.0f);
    light->set_range           (range);
    light->set_inner_spot_angle(0.2f * glm::pi<float>());
    light->set_outer_spot_angle(0.3f * glm::pi<float>());
    light->set_cast_shadow     (cast_shadow);
    const glm::vec3 direction = target - position;
    const glm::vec3 up        = (std::abs(direction.y) > (0.9f * glm::length(direction))) ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    light->set_world_from_node(erhe::math::create_look_at(position, target, up));
    return light;
}

} // anonymous namespace

class Light_cluster_gpu_test : public Gpu_test
{
protected:
    void SetUp() override
    {
        Gpu_test::SetUp();
        initialize_scene_renderer_test_logging();

        m_camera = std::make_shared<erhe::scene::Camera>("gpu cluster test camera");
        erhe::scene::Projection projection{};
        projection.projection_type    = erhe::scene::Projection::Type::perspective_vertical;
        projection.perspective_z_near = 0.1f;
        projection.perspective_z_far  = 100.0f;
        projection.fov_y              = glm::radians(60.0f);
        m_camera->set_projection(projection);
        m_camera->set_world_from_node(
            erhe::math::create_look_at(glm::vec3{3.0f, 2.0f, 5.0f}, glm::vec3{-1.0f, 0.5f, -6.0f}, glm::vec3{0.0f, 1.0f, 0.0f})
        );
        m_camera_snapshot.capture(*m_camera);

        std::mt19937 random{4242u};
        std::uniform_real_distribution<float> x_distribution{-20.0f, 20.0f};
        std::uniform_real_distribution<float> y_distribution{ -6.0f,  8.0f};
        std::uniform_real_distribution<float> z_distribution{-40.0f, 12.0f};
        std::uniform_real_distribution<float> range_distribution{0.5f, 8.0f};
        m_lights.push_back(make_scene_light(erhe::scene::Light_type::directional, glm::vec3{0.0f, 10.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.5f}, 0.0f, true));
        m_lights.push_back(make_scene_light(erhe::scene::Light_type::directional, glm::vec3{0.0f, 10.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f}, 0.0f, false));
        m_lights.push_back(make_scene_light(erhe::scene::Light_type::spot, glm::vec3{0.0f, 4.0f, -3.0f}, glm::vec3{0.0f, 0.0f, -3.0f}, 0.0f, false));
        for (std::size_t i = 0; i < 40; ++i) {
            for (const erhe::scene::Light_type type : {erhe::scene::Light_type::spot, erhe::scene::Light_type::point}) {
                const glm::vec3 position{x_distribution(random), y_distribution(random), z_distribution(random)};
                const glm::vec3 target  {x_distribution(random), y_distribution(random), z_distribution(random)};
                m_lights.push_back(make_scene_light(type, position, target, range_distribution(random), i < 3));
            }
        }
        resolve_and_apply();
    }

    void resolve_and_apply()
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
        static_cast<void>(m_light_set.resolve(std::span<const std::shared_ptr<erhe::scene::Light>>{m_lights}, limits, 1024));
        m_light_projections.apply(
            m_light_set,
            &m_camera_snapshot,
            m_viewport,
            erhe::math::Viewport{0, 0, 1024, 1024},
            std::shared_ptr<erhe::graphics::Texture>{},
            device().get_reverse_depth(),
            device().get_info().coordinate_conventions.native_depth_range,
            device().get_info().coordinate_conventions
        );
    }

    [[nodiscard]] auto make_view() -> Light_cluster_view
    {
        return Light_cluster_view{
            .camera        = &m_camera_snapshot,
            .viewport      = m_viewport,
            .reverse_depth = device().get_reverse_depth(),
            .depth_range   = device().get_info().coordinate_conventions.native_depth_range,
            .conventions   = device().get_info().coordinate_conventions
        };
    }

    // Bins on the CPU and on the GPU with config and compares the lists,
    // cluster by cluster, and the overflow counters.
    void bin_and_compare(const Light_cluster_config& config)
    {
        Light_cluster_gpu_binner gpu_binner{device(), shader_path()};
        ASSERT_TRUE(gpu_binner.is_supported()) << "light_cluster_bin.comp did not build";

        Light_cluster_binner& binner = m_binner;
        binner.bin(m_light_projections, make_view(), config);
        ASSERT_GT(binner.get_binned_light_count(), 70u);

        const std::size_t byte_count = binner.get_fixed_byte_count();
        std::shared_ptr<erhe::graphics::Buffer> output = make_readback_buffer(byte_count, "light cluster gpu test output");
        Light_cluster_view_state view_state;
        uint64_t frame_index = 0;
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                frame_index = device().get_frame_index();
                gpu_binner.dispatch(command_buffer, binner, *output, 0, byte_count, view_state, frame_index, "test");
            }
        );
        const std::vector<std::byte> data = read_buffer(*output, byte_count);

        const std::size_t cluster_count = binner.get_grid().get_cluster_count();
        const std::size_t list_base     = c_light_cluster_header_uint_count * cluster_count;
        std::size_t       mismatches    = 0;
        std::size_t       listed        = 0;
        for (std::size_t cluster = 0; (cluster < cluster_count) && (mismatches < 10); ++cluster) {
            const std::size_t header      = c_light_cluster_header_uint_count * cluster;
            const uint32_t    list_offset = read_uint(data, header + 0);
            const uint32_t    spot        = read_uint(data, header + 1);
            const uint32_t    point       = read_uint(data, header + 2);
            const uint32_t    gpu_counts[4]{spot & 0xffffu, spot >> 16u, point & 0xffffu, point >> 16u};
            EXPECT_EQ(list_offset, list_base + (cluster * binner.get_fixed_list_stride())) << "cluster " << cluster;
            const std::span<const uint32_t> cpu_counts = binner.get_cluster_category_counts(cluster);
            const std::span<const uint32_t> cpu_slots  = binner.get_cluster_slots(cluster);
            bool same = true;
            for (std::size_t category = 0; category < 4; ++category) {
                same = same && (gpu_counts[category] == cpu_counts[category]);
            }
            const std::size_t gpu_count = gpu_counts[0] + gpu_counts[1] + gpu_counts[2] + gpu_counts[3];
            same = same && (gpu_count == cpu_slots.size());
            for (std::size_t i = 0; same && (i < gpu_count); ++i) {
                const uint32_t packed   = read_uint(data, list_offset + (i >> 1u));
                const uint32_t gpu_slot = ((i & 1u) == 0u) ? (packed & 0xffffu) : (packed >> 16u);
                same = (gpu_slot == cpu_slots[i]);
            }
            listed += cpu_slots.size();
            if (!same) {
                ++mismatches;
                ADD_FAILURE() << "cluster " << cluster << ": GPU list differs from the CPU list (" << cpu_slots.size() << " CPU slots, " << gpu_count << " GPU slots)";
            }
        }
        EXPECT_EQ(mismatches, 0u);
        EXPECT_GT(listed, cluster_count) << "the lists must be exercised";

        // The overflow counters, read back as the frame that reuses the
        // dispatch frame's slot reads them.
        view_state.retire(frame_index + device().get_number_of_frames_in_flight());
        const Light_cluster_stats& stats = view_state.get_stats();
        ASSERT_TRUE(stats.valid);
        EXPECT_EQ(stats.binning, Light_cluster_binning::gpu);
        EXPECT_EQ(stats.cluster_count, cluster_count);
        EXPECT_EQ(stats.overflow_cluster_count, binner.get_overflow_cluster_count());
        EXPECT_EQ(stats.max_list_length, binner.get_max_list_length());
        m_overflow_cluster_count = stats.overflow_cluster_count;
    }

    std::shared_ptr<erhe::scene::Camera>             m_camera;
    erhe::scene::Camera_snapshot                     m_camera_snapshot;
    erhe::math::Viewport                             m_viewport{0, 0, 1334, 1120};
    std::vector<std::shared_ptr<erhe::scene::Light>> m_lights;
    Light_set                                        m_light_set;
    Light_projections                                m_light_projections;
    std::size_t                                      m_overflow_cluster_count{0};
    Light_cluster_binner                             m_binner;
};

TEST_F(Light_cluster_gpu_test, gpu_lists_equal_cpu_lists)
{
    bin_and_compare(Light_cluster_config{});
    EXPECT_EQ(m_overflow_cluster_count, 0u);
}

TEST_F(Light_cluster_gpu_test, gpu_capacity_cut_equals_cpu)
{
    // A capacity below the longest list: overflowing clusters keep their
    // lowest slots on both binners, and the counters agree.
    bin_and_compare(Light_cluster_config{.tile_size = 64, .slice_count = 24, .capacity = 3});
    EXPECT_GT(m_overflow_cluster_count, 0u);
}

TEST_F(Light_cluster_gpu_test, gpu_boundary_lights_equal_cpu)
{
    // Point lights whose range sphere touches a cluster's widened bounds
    // within a few ulps: their membership in that cluster is decided by the
    // last bits of the fp32 distance, where a contracted multiply-add on
    // either side flips the decision.
    const Light_cluster_config config{};
    const std::size_t          base_count = m_lights.size();

    // Probe lights at chosen node-space points: a quarter of a cluster's
    // width beside clusters in the middle slices. Their exact fp32 centres
    // come from the binner's inputs.
    m_binner.prepare(m_light_projections, make_view(), config);
    std::vector<std::byte> inputs(m_binner.get_input_byte_count());
    m_binner.write_inputs(std::span<std::byte>{inputs});
    const auto read_vec4 = [&inputs](const std::size_t index) -> glm::vec4 {
        glm::vec4 value{};
        std::memcpy(&value, inputs.data() + (c_light_cluster_bin_header_uvec4_count * sizeof(glm::uvec4)) + (index * sizeof(glm::vec4)), sizeof(value));
        return value;
    };
    const glm::uvec4 counts      = m_binner.get_grid().counts;
    const uint32_t   tiles_x     = counts.x;
    const uint32_t   tiles_y     = counts.y;
    const uint32_t   slice_count = counts.z;
    ASSERT_GT(slice_count, 16u);
    const auto get_bounds = [&](const uint32_t ix, const uint32_t iy, const uint32_t k, glm::vec3& lo, glm::vec3& hi) {
        const glm::vec4 slice = read_vec4(k);
        for (uint32_t c = 0; c < 4; ++c) {
            const uint32_t  corner    = (((iy + (c >> 1u)) * (tiles_x + 1)) + ix + (c & 1u));
            const glm::vec3 origin    {read_vec4(slice_count + (2 * corner) + 0)};
            const glm::vec3 direction {read_vec4(slice_count + (2 * corner) + 1)};
            const glm::vec3 step_0    = slice.x * direction;
            const glm::vec3 step_1    = slice.y * direction;
            const glm::vec3 p0        = origin + step_0;
            const glm::vec3 p1        = origin + step_1;
            lo = (c == 0) ? glm::min(p0, p1) : glm::min(lo, glm::min(p0, p1));
            hi = (c == 0) ? glm::max(p0, p1) : glm::max(hi, glm::max(p0, p1));
        }
        lo -= glm::vec3{slice.z};
        hi += glm::vec3{slice.z};
    };
    class Target
    {
    public:
        uint32_t  cluster;
        glm::vec3 lo;
        glm::vec3 hi;
    };
    std::vector<Target> targets;
    const glm::mat4 world_from_node = m_camera_snapshot.world_from_node();
    for (const uint32_t k : {8u, 12u, 16u}) {
        for (const glm::uvec2 tile : {glm::uvec2{7u, 6u}, glm::uvec2{12u, 10u}, glm::uvec2{15u, 4u}}) {
            Target target{.cluster = (((k * tiles_y) + tile.y) * tiles_x) + tile.x};
            get_bounds(tile.x, tile.y, k, target.lo, target.hi);
            targets.push_back(target);
            // Beside the bounds on all three axes: the nearest point is a
            // corner, so every axis enters the distance.
            const glm::vec3 size = target.hi - target.lo;
            const glm::vec3 node_position = target.hi + (0.25f * size);
            const glm::vec3 world_position{world_from_node * glm::vec4{node_position, 1.0f}};
            m_lights.push_back(make_scene_light(erhe::scene::Light_type::point, world_position, world_position + glm::vec3{0.0f, -1.0f, 0.0f}, 1.0f, false));
        }
    }
    resolve_and_apply();
    m_binner.prepare(m_light_projections, make_view(), config);
    inputs.assign(m_binner.get_input_byte_count(), std::byte{0});
    m_binner.write_inputs(std::span<std::byte>{inputs});
    // The probes are the last point unshadowed slots, in scene order.
    const std::size_t light_base  = slice_count + (2 * (tiles_x + 1) * (tiles_y + 1));
    const std::size_t probe_first = m_binner.get_binned_light_count() - targets.size();

    // Replace each probe with a sweep of ranges around the one whose
    // widened radius equals the distance to its cluster's bounds.
    constexpr int sweep = 4;
    m_lights.resize(base_count);
    std::vector<std::size_t> sweep_first_index;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const glm::vec4 light   = read_vec4(light_base + probe_first + i);
        const glm::vec3 centre  {light};
        const glm::vec3 nearest = glm::clamp(centre, targets[i].lo, targets[i].hi);
        const glm::vec3 offset  = centre - nearest;
        const float     xx      = offset.x * offset.x;
        const float     yy      = offset.y * offset.y;
        const float     zz      = offset.z * offset.z;
        const float     xy      = xx + yy;
        const float     radius  = std::sqrt(xy + zz);
        float           range   = static_cast<float>(static_cast<double>(radius) / (1.0 + (1.0 / 1024.0)));
        for (int step = 0; step < sweep; ++step) {
            range = std::nextafter(range, 0.0f);
        }
        const glm::vec3 world_position{world_from_node * glm::vec4{centre, 1.0f}};
        sweep_first_index.push_back(m_lights.size() - base_count);
        for (int step = -sweep; step <= sweep; ++step) {
            m_lights.push_back(make_scene_light(erhe::scene::Light_type::point, world_position, world_position + glm::vec3{0.0f, -1.0f, 0.0f}, range, false));
            range = std::nextafter(range, 1.0e9f);
        }
    }
    resolve_and_apply();
    bin_and_compare(config);

    // The sweeps straddle the decision: in every sweep the target cluster
    // lists the larger ranges and not the smaller ones.
    const std::size_t sweep_first_slot = m_binner.get_binned_light_count() - (targets.size() * ((2 * sweep) + 1));
    std::size_t straddling = 0;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const std::span<const uint32_t> slots = m_binner.get_cluster_slots(targets[i].cluster);
        std::size_t listed = 0;
        for (int step = 0; step <= (2 * sweep); ++step) {
            const uint32_t slot = static_cast<uint32_t>(sweep_first_slot + (i * ((2 * sweep) + 1)) + static_cast<std::size_t>(step));
            if (std::find(slots.begin(), slots.end(), slot) != slots.end()) {
                ++listed;
            }
        }
        if ((listed > 0) && (listed < static_cast<std::size_t>((2 * sweep) + 1))) {
            ++straddling;
        }
    }
    EXPECT_GT(straddling, targets.size() / 2) << "the sweeps must straddle the fp32 decision";
}

} // namespace erhe::scene_renderer::test
