#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/math_util.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// Fullscreen triangle; each pass restricts the actually-written region with a
// scissor rect rather than with geometry, so both draws cover the viewport and
// the scissor decides which half is touched.
constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

// Constant color injected via COLOR define (an rgba vec4 literal).
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = COLOR;
}
)glsl";

auto build_color_shader(
    erhe::graphics::Device&                 device,
    const erhe::graphics::Bind_group_layout& layout,
    const erhe::graphics::Fragment_outputs&  fragment_outputs,
    const char*                              color_define,
    const char*                              name
) -> erhe::graphics::Shader_stages
{
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = name,
        .defines          = { { "COLOR", color_define } },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device, shader_create_info);
    EXPECT_TRUE(prototype.is_valid()) << name << " shader failed to compile/link";
    return erhe::graphics::Shader_stages{device, std::move(prototype)};
}

} // namespace

// Load_action::Load coverage: two render passes recorded into ONE command
// buffer, both targeting the SAME color texture.
//
//   Pass 1: Load_action::Clear (to black) + Store. Scissored to the LEFT half,
//           draws red there. layout_after = color_attachment_optimal.
//   Pass 2: Load_action::Load (PRESERVE pass 1) + Store. layout_before MUST
//           equal pass 1's layout_after (color_attachment_optimal). Scissored to
//           the RIGHT half, draws green there. layout_after = transfer_src so the
//           result can be read back.
//
// The two scissored regions do NOT overlap, so the left half is never touched by
// pass 2. Reading back, the left half must still be red (pass 1's result) and the
// right half must be green. If pass 2 had used Load_action::Clear instead of Load,
// the left half would have been wiped to black -- so an asserted-red left half is
// exactly the proof that Load preserved the prior contents.
TEST_F(Gpu_test, load_action_preserves_prior_pass)
{
    constexpr int width      = 16;
    constexpr int height     = 16;
    constexpr int half_width = width / 2;

    erhe::graphics::Device& graphics_device = device();

    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    const erhe::graphics::Bind_group_layout empty_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"load_action empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages red_shader   = build_color_shader(graphics_device, empty_layout, fragment_outputs, "vec4(1.0, 0.0, 0.0, 1.0)", "load_action_red");
    erhe::graphics::Shader_stages green_shader = build_color_shader(graphics_device, empty_layout, fragment_outputs, "vec4(0.0, 1.0, 0.0, 1.0)", "load_action_green");

    // Pass 1: clear to black, draw red into the left half, leave the image in
    // color_attachment_optimal so pass 2 can Load it without a layout transition
    // between the two passes.
    erhe::graphics::Render_pass_descriptor pass1_descriptor{};
    pass1_descriptor.color_attachments[0].texture       = color_target.get();
    pass1_descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 }; // black
    pass1_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    pass1_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    pass1_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    pass1_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    pass1_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    pass1_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::color_attachment_optimal;
    pass1_descriptor.render_target_width  = width;
    pass1_descriptor.render_target_height = height;
    pass1_descriptor.debug_label = erhe::utility::Debug_label{"load_action pass 1 (clear)"};

    // Pass 2: LOAD the prior result (layout_before matches pass 1 layout_after),
    // draw green into the right half, end in transfer_src for readback.
    erhe::graphics::Render_pass_descriptor pass2_descriptor{};
    pass2_descriptor.color_attachments[0].texture       = color_target.get();
    pass2_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Load;
    pass2_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    pass2_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    pass2_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::color_attachment_optimal;
    pass2_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    pass2_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    pass2_descriptor.render_target_width  = width;
    pass2_descriptor.render_target_height = height;
    pass2_descriptor.debug_label = erhe::utility::Debug_label{"load_action pass 2 (load)"};

    erhe::graphics::Render_pipeline_create_info red_pipeline_create_info;
    red_pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    red_pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    red_pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    red_pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    red_pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    red_pipeline_create_info.base.bind_group_layout                 = &empty_layout;
    red_pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    red_pipeline_create_info.shader_stages                          = &red_shader;
    red_pipeline_create_info.vertex_input                           = nullptr;
    red_pipeline_create_info.set_format_from_render_pass(pass1_descriptor);
    const erhe::graphics::Render_pipeline red_pipeline{graphics_device, red_pipeline_create_info};
    ASSERT_TRUE(red_pipeline.is_valid()) << "load_action red pipeline is not valid";

    erhe::graphics::Render_pipeline_create_info green_pipeline_create_info;
    green_pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    green_pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    green_pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    green_pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    green_pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    green_pipeline_create_info.base.bind_group_layout                 = &empty_layout;
    green_pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    green_pipeline_create_info.shader_stages                          = &green_shader;
    green_pipeline_create_info.vertex_input                           = nullptr;
    green_pipeline_create_info.set_format_from_render_pass(pass2_descriptor);
    const erhe::graphics::Render_pipeline green_pipeline{graphics_device, green_pipeline_create_info};
    ASSERT_TRUE(green_pipeline.is_valid()) << "load_action green pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            // Pass 1: clear black, red into the left half.
            {
                erhe::graphics::Render_pass            render_pass{graphics_device, pass1_descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, width, height);
                encoder.set_scissor_rect (0, 0, half_width, height); // left half only
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(red_pipeline);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
            // Pass 2: LOAD prior contents, green into the right half.
            {
                erhe::graphics::Render_pass            render_pass{graphics_device, pass2_descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, width, height);
                encoder.set_scissor_rect (half_width, 0, width - half_width, height); // right half only
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(green_pipeline);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    auto texel = [&](const int x, const int y) -> std::array<int, 4> {
        const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
        return std::array<int, 4>{
            static_cast<int>(pixels[index + 0u]),
            static_cast<int>(pixels[index + 1u]),
            static_cast<int>(pixels[index + 2u]),
            static_cast<int>(pixels[index + 3u])
        };
    };

    int left_red    = 0; // left half still carries pass 1's red (proves Load preserved it)
    int left_black  = 0; // left half was wiped to black (would indicate Load behaved like Clear)
    int right_green = 0; // right half carries pass 2's green
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::array<int, 4> c = texel(x, y);
            const bool is_red   = (c[0] >= 253) && (c[1] <= 2)   && (c[2] <= 2)   && (c[3] >= 253);
            const bool is_green = (c[0] <= 2)   && (c[1] >= 253) && (c[2] <= 2)   && (c[3] >= 253);
            const bool is_black = (c[0] <= 2)   && (c[1] <= 2)   && (c[2] <= 2)   && (c[3] >= 253);
            if (x < half_width) {
                if (is_red)   ++left_red;
                if (is_black) ++left_black;
            } else {
                if (is_green) ++right_green;
            }
        }
    }

    const int half_texels = half_width * height;
    // The whole left half must be red and untouched by pass 2 (the crux of Load vs Clear).
    EXPECT_EQ(left_red,   half_texels) << "left half should still be pass 1's red after a Load pass 2";
    EXPECT_EQ(left_black, 0)           << "left half went black -> Load behaved like Clear (regression)";
    // The whole right half must be pass 2's green.
    EXPECT_EQ(right_green, (width - half_width) * height) << "right half should be pass 2's green";
}

// Render pass load actions, ported from the agfx tests RenderPassActionClear,
// RenderPassActionDontCare and RenderPassActionLoad.
//
// Each case seeds a 64x64 format_8_vec4_unorm attachment with a gradient
// (copy_from_buffer), then runs one render pass with the load action under
// test and one draw:
//   - Clear:     a centred triangle over the clear color; the four 16x16
//                corners must equal the clear color.
//   - Dont_care: a fullscreen triangle; the prior content is undefined, so
//                only a full-coverage draw has a defined result, and every
//                texel must equal the draw color.
//   - Load:      a centred triangle over the seed; the four 16x16 corners must
//                equal the seed byte for byte.
// The centred triangle spans NDC [-0.5, 0.5] on both axes, apex at +y, so it
// never reaches a corner region, and the target centre texel is inside it.
//
// The seed is defined in image space (row 0 = image top: red grows to the
// right, green grows downward) and written to memory rows through the device's
// texture_origin, so the golden shows the same picture on every backend. The
// corner checks compare memory-space readback against the memory-space seed,
// and the four corners map onto themselves under the row flip.

namespace {

constexpr int c_action_size   = 64;
constexpr int c_action_corner = 16;

constexpr std::uintptr_t c_centred_triangle_first    = 0;
constexpr std::uintptr_t c_fullscreen_triangle_first = 3;

constexpr const char* c_action_vertex_source = R"glsl(
void main()
{
    vec2 positions[6] = vec2[6](
        vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.0,  0.5),
        vec2(-1.0, -1.0), vec2( 3.0, -1.0), vec2(-1.0,  3.0)
    );
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

// Clear value and draw color; both convert exactly to 8-bit unorm.
constexpr std::array<double, 4>  c_action_clear_value{0.2, 0.4, 0.6, 1.0};
constexpr std::array<uint8_t, 4> c_action_clear_bytes{51u, 102u, 153u, 255u};
constexpr const char*            c_action_draw_color_glsl = "vec4(1.0, 0.8, 0.0, 1.0)";
constexpr std::array<uint8_t, 4> c_action_draw_bytes{255u, 204u, 0u, 255u};

enum class Action_draw : unsigned int {
    centred_triangle,
    fullscreen_triangle
};

[[nodiscard]] auto action_texel_index(const int x, const int y) -> std::size_t
{
    return ((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_action_size)) + static_cast<std::size_t>(x)) * 4u;
}

[[nodiscard]] auto is_corner_texel(const int x, const int y) -> bool
{
    const bool corner_row    = (y < c_action_corner) || (y >= (c_action_size - c_action_corner));
    const bool corner_column = (x < c_action_corner) || (x >= (c_action_size - c_action_corner));
    return corner_row && corner_column;
}

// Within +-1 per channel.
[[nodiscard]] auto action_texel_near(const std::vector<uint8_t>& pixels, const int x, const int y, const std::array<uint8_t, 4>& expected) -> bool
{
    const std::size_t index = action_texel_index(x, y);
    for (std::size_t c = 0; c < 4u; ++c) {
        if (std::abs(static_cast<int>(pixels[index + c]) - static_cast<int>(expected[c])) > 1) {
            return false;
        }
    }
    return true;
}

} // namespace

class Pass_action_test : public Gpu_test
{
protected:
    // Seed in memory row order (row 0 at the device's texture origin).
    [[nodiscard]] auto make_seed() -> std::vector<uint8_t>
    {
        const bool row0_is_top =
            (device().get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left);
        std::vector<uint8_t> seed(static_cast<std::size_t>(c_action_size) * static_cast<std::size_t>(c_action_size) * 4u);
        for (int memory_row = 0; memory_row < c_action_size; ++memory_row) {
            const int image_y = row0_is_top ? memory_row : (c_action_size - 1 - memory_row);
            for (int x = 0; x < c_action_size; ++x) {
                const std::size_t index = action_texel_index(x, memory_row);
                seed[index + 0u] = static_cast<uint8_t>(x * 4);
                seed[index + 1u] = static_cast<uint8_t>(image_y * 4);
                seed[index + 2u] = 96u;
                seed[index + 3u] = 255u;
            }
        }
        return seed;
    }

    // Seed a fresh attachment, run one pass with load_action and the given
    // draw, and return the read-back texels and the seed (both in memory row
    // order).
    void run_pass(
        const erhe::graphics::Load_action load_action,
        const Action_draw                 draw,
        std::vector<uint8_t>&             out_pixels,
        std::vector<uint8_t>&             out_seed
    )
    {
        erhe::graphics::Device& graphics_device = device();

        const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(
            c_action_size, c_action_size, erhe::dataformat::Format::format_8_vec4_unorm, true
        );

        out_seed = make_seed();
        const std::size_t seed_bytes = out_seed.size();
        const std::shared_ptr<erhe::graphics::Buffer> seed_buffer =
            make_host_buffer(seed_bytes, erhe::graphics::Buffer_usage::transfer_src, "pass action seed");
        {
            const std::span<std::byte> mapped = seed_buffer->map_bytes(0, seed_bytes);
            std::memcpy(mapped.data(), out_seed.data(), seed_bytes);
            seed_buffer->unmap();
        }
        // copy_from_buffer leaves the texture in shader_read_only_optimal.
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(
                    seed_buffer.get(),
                    0,                                                  // source_offset
                    static_cast<std::uintptr_t>(c_action_size) * 4u,    // source_bytes_per_row
                    static_cast<std::uintptr_t>(seed_bytes),            // source_bytes_per_image
                    glm::ivec3{c_action_size, c_action_size, 1},        // source_size
                    color_target.get(),
                    0,                                                  // destination_slice
                    0,                                                  // destination_level
                    glm::ivec3{0, 0, 0}                                 // destination_origin
                );
            }
        );

        const erhe::graphics::Bind_group_layout empty_layout{
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings          = {},
                .debug_label       = erhe::utility::Debug_label{"pass action empty layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };
        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "pass_action",
            .defines          = { { "COLOR", c_action_draw_color_glsl } },
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_action_vertex_source} },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source}      }
            },
            .bind_group_layout = &empty_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
        ASSERT_TRUE(prototype.is_valid()) << "pass action shader failed to compile/link";
        erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = color_target.get();
        descriptor.color_attachments[0].clear_value   = c_action_clear_value;
        descriptor.color_attachments[0].load_action   = load_action;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::sampled;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::shader_read_only_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = c_action_size;
        descriptor.render_target_height = c_action_size;
        descriptor.debug_label = erhe::utility::Debug_label{"pass action"};

        erhe::graphics::Render_pipeline_create_info pipeline_create_info;
        pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
        pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
        pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
        pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
        pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
        pipeline_create_info.base.bind_group_layout                 = &empty_layout;
        pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
        pipeline_create_info.shader_stages                          = &shader_stages;
        pipeline_create_info.vertex_input                           = nullptr;
        pipeline_create_info.set_format_from_render_pass(descriptor);
        const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
        ASSERT_TRUE(pipeline.is_valid()) << "pass action pipeline is not valid";

        const std::uintptr_t first_vertex =
            (draw == Action_draw::centred_triangle) ? c_centred_triangle_first : c_fullscreen_triangle_first;
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, c_action_size, c_action_size);
                encoder.set_scissor_rect (0, 0, c_action_size, c_action_size);
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, first_vertex, 3);
            }
        );

        out_pixels = read_texture_rgba8(*color_target);
    }

    void expect_golden(const char* golden_name, const std::vector<uint8_t>& pixels)
    {
        expect_image_matches_golden(
            golden_name, c_action_size, c_action_size, erhe::dataformat::Format::format_8_vec4_unorm,
            std::as_bytes(std::span<const uint8_t>{pixels})
        );
    }
};

// agfx RenderPassActionClear: the seed is replaced by the clear color; the
// corners (outside the centred triangle) show exactly the clear color.
TEST_F(Pass_action_test, clear)
{
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> seed;
    run_pass(erhe::graphics::Load_action::Clear, Action_draw::centred_triangle, pixels, seed);
    ASSERT_EQ(pixels.size(), seed.size());

    int mismatches = 0;
    for (int y = 0; y < c_action_size; ++y) {
        for (int x = 0; x < c_action_size; ++x) {
            if (is_corner_texel(x, y) && !action_texel_near(pixels, x, y, c_action_clear_bytes)) {
                ++mismatches;
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " corner texels differ from the clear color";
    EXPECT_TRUE(action_texel_near(pixels, c_action_size / 2, c_action_size / 2, c_action_draw_bytes)) << "centre texel is not the draw color";
    expect_golden("pass_action_clear", pixels);
}

// agfx RenderPassActionDontCare: the prior content is undefined, so the pass
// draws a fullscreen triangle and every texel must be the draw color.
TEST_F(Pass_action_test, dont_care)
{
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> seed;
    run_pass(erhe::graphics::Load_action::Dont_care, Action_draw::fullscreen_triangle, pixels, seed);
    ASSERT_EQ(pixels.size(), seed.size());

    int mismatches = 0;
    for (int y = 0; y < c_action_size; ++y) {
        for (int x = 0; x < c_action_size; ++x) {
            if (!action_texel_near(pixels, x, y, c_action_draw_bytes)) {
                ++mismatches;
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " texels differ from the draw color";
    expect_golden("pass_action_dont_care", pixels);
}

// agfx RenderPassActionLoad: the seed survives outside the centred triangle;
// the corners equal the seed byte for byte.
TEST_F(Pass_action_test, load)
{
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> seed;
    run_pass(erhe::graphics::Load_action::Load, Action_draw::centred_triangle, pixels, seed);
    ASSERT_EQ(pixels.size(), seed.size());

    int mismatches = 0;
    for (int y = 0; y < c_action_size; ++y) {
        for (int x = 0; x < c_action_size; ++x) {
            if (!is_corner_texel(x, y)) {
                continue;
            }
            const std::size_t index = action_texel_index(x, y);
            if (std::memcmp(pixels.data() + index, seed.data() + index, 4u) != 0) {
                ++mismatches;
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " corner texels differ from the seed";
    EXPECT_TRUE(action_texel_near(pixels, c_action_size / 2, c_action_size / 2, c_action_draw_bytes)) << "centre texel is not the draw color";
    expect_golden("pass_action_load", pixels);
}

} // namespace erhe::graphics::test
