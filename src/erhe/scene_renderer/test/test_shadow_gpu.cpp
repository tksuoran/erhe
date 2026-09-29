// Shadow sampling GPU tests (doc/erhe/shadows.md "Shadow sampling GPU tests").
//
// A plane head-on to the light stores, in its own shadow map, the same depth
// its receivers compute as the comparison reference: the comparison is a tie,
// and with no bias term that is nonzero for a head-on receiver the verdict is
// decided by last-bit rounding. These tests pin that down per light type,
// light pose, shadow technique (depth, distance), filter / receiver bias mode
// and shadow map depth format:
//
//  - Shadow_tie: sample_light_visibility() at receiver points on the plane with
//    the reference depth offset by -4 .. +4 float ulps (shaders/shadow_tie.frag).
//  - Shadow_head_on_plane: the forward pass with Shader_debug::shadow_visibility
//    over the plane.
//
// Both require visibility 1 everywhere, with no rasterizer bias: the
// receiver's minimum bias (doc/erhe/shadows.md "Minimum bias" and "The
// distance technique") must exceed the stored / reference rounding of every
// pose, technique, filter and format. The reference depth offset k moves the
// light-space depth only, which the distance technique does not compare, so
// for it every k band evaluates the plain tie.
// The k range -4 .. +4 is the Shadow_tie requirement: the bias covers
// the tie itself plus 4 ulps of reference error in either direction. The
// derived bound is 15 to 95 ulps of the reference depth on these poses (the
// first failing k measured with a wider sweep) against at most about 5 ulps of
// measured stored / reference difference, so the range sits well inside it.
// The controls next to them show that the harness sees both outcomes:
//
//  - At the exact pose (identity station frame, where every matrix is exact
//    and the tie is decided at k = 0), a rasterizer constant bias of -4 (the
//    head-on tie evidence) makes the plane read lit with the depth
//    technique (the distance technique stores plane distances, which the
//    rasterizer bias does not move).
//  - A caster box above the plane reads 0 in the interior of its analytic
//    shadow at every pose and with both techniques, so an "always 1" sampler
//    cannot pass the cases above; at the exact pose the plane well outside
//    that shadow reads 1.

#include "shadow_gpu_test_fixture.hpp"

#include "erhe_graphics/device.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <ostream>
#include <span>
#include <string>
#include <vector>

namespace erhe::scene_renderer::test {

namespace {

// The head-on tie evidence: a rasterizer constant bias of -4 (signed
// toward the light) removes the head-on tie.
constexpr float c_control_depth_bias_constant = -4.0f;

// Caster box of the occlusion control, centered above the origin. Its analytic
// shadow on the plane contains x, z in [-c_box_half_size, c_box_half_size] for
// both lights (the spot light above its center only enlarges it), and lies
// within |x|, |z| <= c_box_half_size * h / (h - c_box_top), at most 0.4 for
// the lowest spot pose h = 2.
constexpr float c_box_half_size    = 0.25f;
constexpr float c_box_bottom       = 0.5f;
constexpr float c_box_top          = 0.75f;
// Interior: at least 5 cm inside the analytic shadow, several shadow texels
// more than the widest filter footprint. Exterior: at least 10 cm outside it.
constexpr float c_interior_extent  = 0.2f;
constexpr float c_exterior_extent  = 0.5f;

class Visibility_counts
{
public:
    int checked  {0};
    int failing  {0};
    int unwritten{0};
};

// Pixels of the region (interior: |x|, |z| < extent; exterior: |x| or
// |z| > extent, in station coordinates) whose visibility is not the expected
// value.
enum class Region : unsigned int
{
    everywhere,
    interior,
    exterior
};

[[nodiscard]] auto count_visibility(const Visibility_image& image, const Region region, const float extent, const float expected) -> Visibility_counts
{
    Visibility_counts counts{};
    for (int y = 0; y < image.size; ++y) {
        for (int x = 0; x < image.size; ++x) {
            const float sx = std::abs(image.station_x(x));
            const float sz = std::abs(image.station_z(y));
            const bool  in_region =
                (region == Region::everywhere) ||
                ((region == Region::interior) && (sx < extent) && (sz < extent)) ||
                ((region == Region::exterior) && ((sx > extent) || (sz > extent)));
            if (!in_region) {
                continue;
            }
            const float visibility = image.at(x, y);
            ++counts.checked;
            if (visibility < 0.0f) {
                ++counts.unwritten; // cleared to -1: the plane did not cover the pixel
            } else if (visibility != expected) {
                ++counts.failing;
            }
        }
    }
    return counts;
}

[[nodiscard]] auto describe_case(
    const Shadow_light_kind        light_kind,
    const Shadow_pose&             pose,
    const Shadow_technique_case    technique,
    const erhe::dataformat::Format depth_format,
    const Shadow_filter_case&      filter_case,
    const float                    depth_bias_constant
) -> std::string
{
    return describe(light_kind, pose) +
        " " + c_str(technique) +
        " " + erhe::dataformat::c_str(depth_format) +
        " " + filter_case.name +
        " rasterizer constant bias " + std::to_string(depth_bias_constant);
}

// Every pose, and the exact pose (identity station frame) alone.
[[nodiscard]] auto all_poses() -> std::span<const Shadow_pose>
{
    return std::span<const Shadow_pose>{get_shadow_poses()};
}

[[nodiscard]] auto exact_pose() -> std::span<const Shadow_pose>
{
    return all_poses().first(1);
}

// Both techniques, and the depth technique alone (the rasterizer bias
// controls).
[[nodiscard]] auto all_techniques() -> std::span<const Shadow_technique_case>
{
    return std::span<const Shadow_technique_case>{get_shadow_technique_cases()};
}

[[nodiscard]] auto depth_technique() -> std::span<const Shadow_technique_case>
{
    return all_techniques().first(1);
}

} // anonymous namespace

// gtest value printer: names the parameter in failure output and in the ctest
// case names gtest_discover_tests derives from it.
void PrintTo(const Shadow_light_kind light_kind, std::ostream* out)
{
    *out << c_str(light_kind);
}

class Shadow_light_gpu_test
    : public Shadow_gpu_test
    , public ::testing::WithParamInterface<Shadow_light_kind>
{
protected:
    // Shadow_tie over the poses, techniques, every depth format and filter
    // case: every receiver point reads 1 for every reference depth offset.
    void check_tie(
        const std::span<const Shadow_pose>           poses,
        const std::span<const Shadow_technique_case> techniques,
        const float                                  depth_bias_constant
    )
    {
        const Shadow_light_kind light_kind = GetParam();
        for (const Shadow_pose& pose : poses) {
            set_light(light_kind, pose);
            for (const Shadow_technique_case technique : techniques) {
                for (const erhe::dataformat::Format depth_format : get_shadow_depth_formats()) {
                    render_shadow_map(
                        Shadow_map_settings{
                            .depth_format        = depth_format,
                            .technique           = technique,
                            .cull_mode           = Shadow_cull_mode::cull_back,
                            .depth_bias_constant = depth_bias_constant
                        }
                    );
                    for (const Shadow_filter_case& filter_case : get_shadow_filter_cases()) {
                        const Shadow_tie_result result = render_tie(filter_case);
                        EXPECT_EQ(result.total_failing(), 0)
                            << describe_case(light_kind, pose, technique, depth_format, filter_case, depth_bias_constant) << ": " << result.describe();
                    }
                }
            }
        }
    }

    // Shadow_head_on_plane over the poses, techniques, every depth format and
    // filter case: every pixel of the forward shadow_visibility pass reads 1.
    void check_head_on_plane(
        const std::span<const Shadow_pose>           poses,
        const std::span<const Shadow_technique_case> techniques,
        const float                                  depth_bias_constant
    )
    {
        const Shadow_light_kind light_kind = GetParam();
        for (const Shadow_pose& pose : poses) {
            set_light(light_kind, pose);
            for (const Shadow_technique_case technique : techniques) {
                for (const erhe::dataformat::Format depth_format : get_shadow_depth_formats()) {
                    render_shadow_map(
                        Shadow_map_settings{
                            .depth_format        = depth_format,
                            .technique           = technique,
                            .cull_mode           = Shadow_cull_mode::cull_back,
                            .depth_bias_constant = depth_bias_constant
                        }
                    );
                    for (const Shadow_filter_case& filter_case : get_shadow_filter_cases()) {
                        const Visibility_image  image  = render_visibility(filter_case);
                        const Visibility_counts counts = count_visibility(image, Region::everywhere, 0.0f, 1.0f);
                        const std::string       label  = describe_case(light_kind, pose, technique, depth_format, filter_case, depth_bias_constant);
                        EXPECT_EQ(counts.unwritten, 0) << label;
                        EXPECT_EQ(counts.failing,   0) << label << ": " << counts.failing << " of " << counts.checked << " pixels read less than 1";
                    }
                }
            }
        }
    }
};

TEST_P(Shadow_light_gpu_test, shadow_tie_head_on_plane_reads_lit)
{
    check_tie(all_poses(), all_techniques(), 0.0f);
}

TEST_P(Shadow_light_gpu_test, shadow_head_on_plane_reads_lit)
{
    check_head_on_plane(all_poses(), all_techniques(), 0.0f);
}

// Control: at the exact pose the rasterizer constant bias moves the stored
// plane away from the light by more than the reference offsets (it was the
// only thing that kept k < 0 lit before the receiver's minimum bias), so the
// tie harness reads lit with the caster-side bias on top of the receiver's.
TEST_P(Shadow_light_gpu_test, shadow_tie_exact_pose_with_rasterizer_bias_reads_lit)
{
    check_tie(exact_pose(), depth_technique(), c_control_depth_bias_constant);
}

// Control: the same through the forward shadow_visibility pass.
TEST_P(Shadow_light_gpu_test, shadow_head_on_plane_exact_pose_with_rasterizer_bias_reads_lit)
{
    check_head_on_plane(exact_pose(), depth_technique(), c_control_depth_bias_constant);
}

// Control: a caster box above the plane reads 0 in the interior of its
// analytic shadow at every pose and with both techniques (so an "always 1"
// sampler cannot pass the cases above), and at the exact pose 1 well outside
// it.
TEST_P(Shadow_light_gpu_test, shadow_caster_box_occludes_plane)
{
    const Shadow_light_kind light_kind = GetParam();
    add_box(
        glm::vec3{-c_box_half_size, c_box_bottom, -c_box_half_size},
        glm::vec3{ c_box_half_size, c_box_top,     c_box_half_size}
    );
    const Shadow_pose* const exact = exact_pose().data();
    for (const Shadow_pose& pose : all_poses()) {
        set_light(light_kind, pose);
        for (const Shadow_technique_case technique : all_techniques()) {
            for (const erhe::dataformat::Format depth_format : get_shadow_depth_formats()) {
                render_shadow_map(
                    Shadow_map_settings{
                        .depth_format        = depth_format,
                        .technique           = technique,
                        .cull_mode           = Shadow_cull_mode::cull_back,
                        .depth_bias_constant = c_control_depth_bias_constant
                    }
                );
                for (const Shadow_filter_case& filter_case : get_shadow_filter_cases()) {
                    const Visibility_image  image    = render_visibility(filter_case);
                    const Visibility_counts interior = count_visibility(image, Region::interior, c_interior_extent, 0.0f);
                    const std::string       label    = describe_case(light_kind, pose, technique, depth_format, filter_case, c_control_depth_bias_constant);
                    EXPECT_GT(interior.checked,   0) << label;
                    EXPECT_EQ(interior.unwritten, 0) << label;
                    EXPECT_EQ(interior.failing,   0) << label << ": " << interior.failing << " of " << interior.checked << " shadow interior pixels read more than 0";
                    if (&pose == exact) {
                        const Visibility_counts exterior = count_visibility(image, Region::exterior, c_exterior_extent, 1.0f);
                        EXPECT_EQ(exterior.unwritten, 0) << label;
                        EXPECT_EQ(exterior.failing,   0) << label << ": " << exterior.failing << " of " << exterior.checked << " lit exterior pixels read less than 1";
                    }
                }
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    Lights,
    Shadow_light_gpu_test,
    ::testing::Values(Shadow_light_kind::spot, Shadow_light_kind::directional),
    [](const ::testing::TestParamInfo<Shadow_light_kind>& info) -> std::string {
        return c_str(info.param);
    }
);

} // namespace erhe::scene_renderer::test
