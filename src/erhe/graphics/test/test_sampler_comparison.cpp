#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Comparison samplers, ported from the agfx tests SamplerComparisonNever /
// Less / Equal / LessEqual / Greater / NotEqual / GreaterEqual; `always` is an
// eighth erhe case (agfx uses it as its non-comparison sentinel).
//
// A 32-bit float depth texture (format_d32_sfloat) is cleared to exactly 0.5
// by a depth-only render pass that leaves it in
// depth_stencil_read_only_optimal, the layout a depth-aspect sampled image is
// bound in. A fullscreen fragment pass (agfx samples from compute; see
// doc/plans/graphics_tests_agfx_port.md 2.3) then samples it through a
// sampler2DShadow binding (sampler_aspect depth) whose sampler has
// compare_enable and the compare operation under test, with nearest filtering
// so each lookup is a single comparison. The 96x32 output has three 32-pixel
// columns in image space, with reference values 0.25, 0.5 and 0.75; each
// lookup returns 1.0 where (reference OP 0.5) holds and 0.0 otherwise (the
// reference is the left operand in both Vulkan and GL). The column is written
// green for 1.0 and red for 0.0. Every texel is checked against the analytic
// band results, then the image against its golden.
//
// The comparison sampler is also installed as the binding's immutable sampler
// (as the engine's shadow samplers are), which is what the Vulkan portability
// subset requires for comparison samplers; set_sampled_image still receives it.

namespace erhe::graphics::test {

namespace {

constexpr int   c_band_width    = 32;
constexpr int   c_band_count    = 3;
constexpr int   c_output_width  = c_band_width * c_band_count;
constexpr int   c_output_height = 32;
constexpr int   c_depth_size    = 16;
constexpr float c_stored_depth  = 0.5f;

constexpr std::array<float, c_band_count> c_references{ 0.25f, 0.5f, 0.75f };

constexpr const char* c_fragment_source = R"glsl(
void main()
{
    int   band      = int(IMAGE_POSITION.x) / BAND_WIDTH;
    float reference = (band == 0) ? 0.25 : ((band == 1) ? 0.5 : 0.75);
    float result    = texture(s_shadow, vec3(gl_FragCoord.xy / vec2(float(TARGET_WIDTH), float(TARGET_HEIGHT)), reference));
    out_color = vec4(1.0 - result, result, 0.0, 1.0);
}
)glsl";

// The value `reference OP stored` for one compare operation.
[[nodiscard]] auto compare(const erhe::graphics::Compare_operation operation, const float reference, const float stored) -> bool
{
    switch (operation) {
        case erhe::graphics::Compare_operation::never:            return false;
        case erhe::graphics::Compare_operation::less:             return reference <  stored;
        case erhe::graphics::Compare_operation::equal:            return reference == stored;
        case erhe::graphics::Compare_operation::less_or_equal:    return reference <= stored;
        case erhe::graphics::Compare_operation::greater:          return reference >  stored;
        case erhe::graphics::Compare_operation::not_equal:        return reference != stored;
        case erhe::graphics::Compare_operation::greater_or_equal: return reference >= stored;
        case erhe::graphics::Compare_operation::always:
        default:                                                  return true;
    }
}

} // namespace

class Sampler_comparison_test : public Gpu_test
{
protected:
    void check_compare(const erhe::graphics::Compare_operation operation, const std::array<bool, c_band_count>& expected_bands, const char* golden_name)
    {
        // The analytic model and the stated band table agree.
        for (std::size_t band = 0; band < c_band_count; ++band) {
            ASSERT_EQ(compare(operation, c_references[band], c_stored_depth), expected_bands[band]) << "band table for " << golden_name;
        }

        const erhe::dataformat::Format depth_format = find_depth32f_format();
        if (depth_format == erhe::dataformat::Format::format_undefined) {
            GTEST_SKIP() << "no pure 32-bit-float depth-renderable format available on this device";
        }
        erhe::graphics::Device& graphics_device = device();

        const std::shared_ptr<erhe::graphics::Texture> depth_texture = std::make_shared<erhe::graphics::Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = graphics_device,
                .usage_mask  =
                    erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
                    erhe::graphics::Image_usage_flag_bit_mask::sampled,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = depth_format,
                .width       = c_depth_size,
                .height      = c_depth_size,
                .debug_label = erhe::utility::Debug_label{"comparison depth texture"}
            }
        );

        // Depth-only pass: clear to exactly 0.5, leave the texture ready to be
        // sampled through its depth aspect.
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass_descriptor descriptor{};
                descriptor.depth_attachment.texture        = depth_texture.get();
                descriptor.depth_attachment.clear_value[0] = static_cast<double>(c_stored_depth);
                descriptor.depth_attachment.load_action    = erhe::graphics::Load_action::Clear;
                descriptor.depth_attachment.store_action   = erhe::graphics::Store_action::Store;
                descriptor.depth_attachment.usage_before   = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
                descriptor.depth_attachment.layout_before  = erhe::graphics::Image_layout::undefined;
                descriptor.depth_attachment.usage_after    = erhe::graphics::Image_usage_flag_bit_mask::sampled;
                descriptor.depth_attachment.layout_after   = erhe::graphics::Image_layout::depth_stencil_read_only_optimal;
                descriptor.render_target_width  = c_depth_size;
                descriptor.render_target_height = c_depth_size;
                descriptor.debug_label = erhe::utility::Debug_label{"comparison depth clear"};
                erhe::graphics::Render_pass              render_pass{graphics_device, descriptor};
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            }
        );

        const erhe::graphics::Sampler sampler{
            graphics_device,
            erhe::graphics::Sampler_create_info{
                .min_filter        = erhe::graphics::Filter::nearest,
                .mag_filter        = erhe::graphics::Filter::nearest,
                .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .address_mode      = {
                    erhe::graphics::Sampler_address_mode::clamp_to_edge,
                    erhe::graphics::Sampler_address_mode::clamp_to_edge,
                    erhe::graphics::Sampler_address_mode::clamp_to_edge
                },
                .compare_enable    = true,
                .compare_operation = operation,
                .debug_label       = erhe::utility::Debug_label{golden_name}
            }
        };
        const erhe::graphics::Bind_group_layout layout{
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point     = 0,
                        .type              = erhe::graphics::Binding_type::combined_image_sampler,
                        .sampler_aspect    = erhe::graphics::Sampler_aspect::depth,
                        .name              = "s_shadow",
                        .glsl_type         = erhe::graphics::Glsl_type::sampler_2d_shadow,
                        .immutable_sampler = &sampler,
                        .stage_flags       = erhe::graphics::Shader_stage_flags::fragment
                    }
                },
                .debug_label       = erhe::utility::Debug_label{"comparison layout"},
                .uses_texture_heap = false
            }
        };
        const std::array<Sampled_image, 1> images{ Sampled_image{ .binding_point = 0, .texture = depth_texture.get(), .sampler = &sampler } };
        const std::shared_ptr<erhe::graphics::Texture> output = render_fullscreen_pass(
            layout,
            c_fragment_source,
            { { "BAND_WIDTH", std::to_string(c_band_width) } },
            images,
            c_output_width,
            c_output_height
        );

        const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_output_width) * static_cast<std::size_t>(c_output_height) * 4u);

        std::vector<uint8_t> expected(pixels.size());
        for (int y = 0; y < c_output_height; ++y) {
            for (int x = 0; x < c_output_width; ++x) {
                const bool        pass  = expected_bands[static_cast<std::size_t>(x / c_band_width)];
                const std::size_t index = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_output_width)) + static_cast<std::size_t>(x)) * 4u;
                expected[index + 0u] = pass ? 0u : 255u;
                expected[index + 1u] = pass ? 255u : 0u;
                expected[index + 2u] = 0u;
                expected[index + 3u] = 255u;
            }
        }
        const std::vector<uint8_t> image = memory_rows_to_image_rows(pixels, static_cast<std::size_t>(c_output_width) * 4u, c_output_height);
        expect_rgba8_near(image, expected, c_output_width, c_output_height, 0, golden_name);
        expect_image_matches_golden(golden_name, c_output_width, c_output_height, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
    }
};

// Bands are the references 0.25 / 0.5 / 0.75 against the stored 0.5.

// agfx SamplerComparisonNever: fails everywhere.
TEST_F(Sampler_comparison_test, never)
{
    check_compare(erhe::graphics::Compare_operation::never, { false, false, false }, "sampler_comparison_never");
}

// agfx SamplerComparisonLess: only 0.25 < 0.5.
TEST_F(Sampler_comparison_test, less)
{
    check_compare(erhe::graphics::Compare_operation::less, { true, false, false }, "sampler_comparison_less");
}

// agfx SamplerComparisonEqual: only 0.5 == 0.5.
TEST_F(Sampler_comparison_test, equal)
{
    check_compare(erhe::graphics::Compare_operation::equal, { false, true, false }, "sampler_comparison_equal");
}

// agfx SamplerComparisonLessEqual: 0.25 and 0.5.
TEST_F(Sampler_comparison_test, less_or_equal)
{
    check_compare(erhe::graphics::Compare_operation::less_or_equal, { true, true, false }, "sampler_comparison_less_or_equal");
}

// agfx SamplerComparisonGreater: only 0.75 > 0.5.
TEST_F(Sampler_comparison_test, greater)
{
    check_compare(erhe::graphics::Compare_operation::greater, { false, false, true }, "sampler_comparison_greater");
}

// agfx SamplerComparisonNotEqual: 0.25 and 0.75.
TEST_F(Sampler_comparison_test, not_equal)
{
    check_compare(erhe::graphics::Compare_operation::not_equal, { true, false, true }, "sampler_comparison_not_equal");
}

// agfx SamplerComparisonGreaterEqual: 0.5 and 0.75.
TEST_F(Sampler_comparison_test, greater_or_equal)
{
    check_compare(erhe::graphics::Compare_operation::greater_or_equal, { false, true, true }, "sampler_comparison_greater_or_equal");
}

// erhe-only: always passes everywhere (agfx's non-comparison sentinel).
TEST_F(Sampler_comparison_test, always)
{
    check_compare(erhe::graphics::Compare_operation::always, { true, true, true }, "sampler_comparison_always");
}

} // namespace erhe::graphics::test
