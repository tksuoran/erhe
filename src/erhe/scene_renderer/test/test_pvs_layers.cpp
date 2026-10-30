// The depth layer function of the potentially visible set bake's layered
// buffer (doc/plans/pvs.md "Bake" 3, erhe_scene_renderer/pvs_layers.hpp).
// No device: pvs_layer_of_depth() is the C++ twin of
// erhe_pvs_layer_of_depth() (res/shaders/erhe_pvs_layer.glsl), the same
// float operations in the same order - z / z_near, log, divided by
// log(z_far / z_near), times the layer count, floor, clamp - so these
// properties are the ones the shader's layers have, up to the GPU's log
// rounding at a boundary.
//
// The ranges are the bake's: z_near = half a cell plus 1 cm (the sample
// point at the cell center, "Bake" 2) for 1 m and 2 m cells and a 0.1 m
// cell, z_far = a scene a few hundred meters across; layer counts 64 (the
// default) and 32.

#include <gtest/gtest.h>

#include "erhe_scene_renderer/pvs_layers.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace {

class Layer_range
{
public:
    float z_near;
    float z_far;
};

constexpr std::array<Layer_range, 4> c_ranges{
    Layer_range{0.51f,  100.0f},
    Layer_range{1.01f,   35.5f},
    Layer_range{0.06f,  500.0f},
    Layer_range{0.51f,    0.75f}
};
constexpr std::array<uint32_t, 2> c_layer_counts{64u, 32u};

} // anonymous namespace

TEST(Pvs_layers, front_depths_are_monotone_and_cover_the_range)
{
    using erhe::scene_renderer::pvs_layer_front_depth;
    for (const Layer_range& range : c_ranges) {
        for (const uint32_t layer_count : c_layer_counts) {
            EXPECT_EQ(pvs_layer_front_depth(0, range.z_near, range.z_far, layer_count), range.z_near);
            EXPECT_EQ(pvs_layer_front_depth(layer_count, range.z_near, range.z_far, layer_count), range.z_far);
            float previous = pvs_layer_front_depth(0, range.z_near, range.z_far, layer_count);
            for (uint32_t k = 1; k <= layer_count; ++k) {
                const float front = pvs_layer_front_depth(k, range.z_near, range.z_far, layer_count);
                EXPECT_GT(front, previous) << "k " << k << " of " << layer_count << " z_near " << range.z_near << " z_far " << range.z_far;
                EXPECT_GE(front, range.z_near);
                EXPECT_LE(front, range.z_far);
                previous = front;
            }
        }
    }
}

TEST(Pvs_layers, layer_of_front_depth_is_the_layer)
{
    using erhe::scene_renderer::pvs_layer_front_depth;
    using erhe::scene_renderer::pvs_layer_of_depth;
    for (const Layer_range& range : c_ranges) {
        for (const uint32_t layer_count : c_layer_counts) {
            for (uint32_t k = 0; k < layer_count; ++k) {
                const float front = pvs_layer_front_depth(k, range.z_near, range.z_far, layer_count);
                EXPECT_EQ(pvs_layer_of_depth(front, range.z_near, range.z_far, layer_count), k)
                    << "k " << k << " of " << layer_count << " z_near " << range.z_near << " z_far " << range.z_far;
                // The float just below a front depth is in the layer before:
                // the front depth is the layer's smallest depth.
                if (k > 0) {
                    const float below = std::nextafter(front, 0.0f);
                    EXPECT_EQ(pvs_layer_of_depth(below, range.z_near, range.z_far, layer_count), k - 1)
                        << "k " << k << " of " << layer_count << " z_near " << range.z_near << " z_far " << range.z_far;
                }
                // Every depth up to the next layer's front is in layer k:
                // the middle of the layer, in the log scale.
                const float back   = pvs_layer_front_depth(k + 1, range.z_near, range.z_far, layer_count);
                const float middle = std::sqrt(front * back);
                EXPECT_EQ(pvs_layer_of_depth(middle, range.z_near, range.z_far, layer_count), k);
            }
        }
    }
}

TEST(Pvs_layers, last_layer_back_is_z_far)
{
    using erhe::scene_renderer::pvs_layer_front_depth;
    using erhe::scene_renderer::pvs_layer_of_depth;
    for (const Layer_range& range : c_ranges) {
        for (const uint32_t layer_count : c_layer_counts) {
            // z_far itself is in the last layer (the clamp), as is every
            // depth from the last layer's front to it.
            EXPECT_EQ(pvs_layer_of_depth(range.z_far, range.z_near, range.z_far, layer_count), layer_count - 1);
            const float last_front = pvs_layer_front_depth(layer_count - 1, range.z_near, range.z_far, layer_count);
            EXPECT_EQ(pvs_layer_of_depth(last_front, range.z_near, range.z_far, layer_count), layer_count - 1);
            EXPECT_LT(last_front, range.z_far);
            // z_near is in the first layer.
            EXPECT_EQ(pvs_layer_of_depth(range.z_near, range.z_near, range.z_far, layer_count), 0u);
        }
    }
}

TEST(Pvs_layers, layers_are_geometric)
{
    using erhe::scene_renderer::pvs_layer_front_depth;
    // Layer k starts at z_near * r^k, r = (z_far / z_near)^(1 / N), to the
    // float the layer function puts there: a relative error of a few ulps.
    for (const Layer_range& range : c_ranges) {
        for (const uint32_t layer_count : c_layer_counts) {
            const double r = std::pow(static_cast<double>(range.z_far) / static_cast<double>(range.z_near), 1.0 / static_cast<double>(layer_count));
            for (uint32_t k = 0; k <= layer_count; ++k) {
                const double expected = static_cast<double>(range.z_near) * std::pow(r, static_cast<double>(k));
                const double front    = static_cast<double>(pvs_layer_front_depth(k, range.z_near, range.z_far, layer_count));
                EXPECT_NEAR(front / expected, 1.0, 1.0e-5) << "k " << k << " of " << layer_count;
            }
        }
    }
}
