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

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Rendering into one subresource of a texture, ported from the agfx tests
// RenderPassClearMip / RenderPassClearSlice / RenderPassClearFace and
// RenderToTextureMip / RenderToTextureSlice / RenderToTextureFace.
//
// Three textures, format_8_vec4_unorm:
//   - mip:   texture_2d, 128x128, two levels;   the target is level 1 (64x64)
//   - slice: texture_2d_array, 64x64, 4 layers; the target is layer 2
//   - face:  texture_cube_map, 64x64;           the target is face 3 (-Y in
//            the Vulkan face order +X, -X, +Y, -Y, +Z, -Z)
// Every subresource (level or layer / face) is seeded with its own solid color
// through copy_from_buffer (destination_level / destination_slice). One render
// pass then targets the subresource through Render_pass_attachment_descriptor
// texture_level / texture_layer:
//   - Clear* cases: Load_action::Clear, no draw; every texel of the target must
//     equal the clear color.
//   - RenderToTexture* cases: Load_action::Load and a centred triangle (NDC
//     [-0.5, 0.5] on both axes, apex at +y); the four 16x16 corners of the
//     target must still equal its seed byte for byte, the centre texel must be
//     the draw color.
// Every other subresource is read back and must equal its seed byte for byte:
// a pass that wrote the wrong level or layer shows up there. The target
// subresource is also asserted against a golden.
//
// The texture stays in shader_read_only_optimal around the pass (usage and
// layout before / after), the layout copy_from_buffer leaves every seeded
// subresource in, so the tracked layout matches every subresource at readback.
// Seeds are solid colors and the corner regions map onto themselves under a
// row flip, so the analytic checks do not depend on the texture origin.

namespace erhe::graphics::test {

namespace {

constexpr int c_corner = 16;

constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.0, 0.5));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = vec4(1.0, 0.8, 0.0, 1.0);
}
)glsl";

// Clear value and draw color; both convert exactly to 8-bit unorm.
constexpr std::array<double, 4>  c_clear_value{0.2, 0.4, 0.6, 1.0};
constexpr std::array<uint8_t, 4> c_clear_bytes{51u, 102u, 153u, 255u};
constexpr std::array<uint8_t, 4> c_draw_bytes {255u, 204u, 0u, 255u};

// Distinct seed color per subresource index (at most six: the cube faces).
constexpr std::array<std::array<uint8_t, 4>, 6> c_seed_colors{{
    {{ 200u,  40u,  40u, 255u }},
    {{  40u, 200u,  40u, 255u }},
    {{  40u,  40u, 200u, 255u }},
    {{ 200u, 200u,  40u, 255u }},
    {{  40u, 200u, 200u, 255u }},
    {{ 200u,  40u, 200u, 255u }}
}};

enum class Target_kind : unsigned int {
    mip,
    slice,
    face
};

enum class Pass_kind : unsigned int {
    clear,
    load_and_draw
};

class Subresource final
{
public:
    unsigned int layer{0};
    unsigned int level{0};
};

class Target_setup final
{
public:
    erhe::graphics::Texture_type type;
    int                          size;
    int                          array_layer_count;
    int                          level_count;
    std::vector<Subresource>     subresources;
    std::size_t                  target_index;
    const char*                  label;
};

[[nodiscard]] auto make_target_setup(const Target_kind kind) -> Target_setup
{
    switch (kind) {
        case Target_kind::mip: {
            return Target_setup{
                .type              = erhe::graphics::Texture_type::texture_2d,
                .size              = 128,
                .array_layer_count = 0,
                .level_count       = 2,
                .subresources      = { Subresource{0, 0}, Subresource{0, 1} },
                .target_index      = 1,
                .label             = "subresource target mip"
            };
        }
        case Target_kind::slice: {
            return Target_setup{
                .type              = erhe::graphics::Texture_type::texture_2d_array,
                .size              = 64,
                .array_layer_count = 4,
                .level_count       = 1,
                .subresources      = { Subresource{0, 0}, Subresource{1, 0}, Subresource{2, 0}, Subresource{3, 0} },
                .target_index      = 2,
                .label             = "subresource target slice"
            };
        }
        case Target_kind::face:
        default: {
            return Target_setup{
                .type              = erhe::graphics::Texture_type::texture_cube_map,
                .size              = 64,
                .array_layer_count = 6,
                .level_count       = 1,
                .subresources      = {
                    Subresource{0, 0}, Subresource{1, 0}, Subresource{2, 0},
                    Subresource{3, 0}, Subresource{4, 0}, Subresource{5, 0}
                },
                .target_index      = 3,
                .label             = "subresource target face"
            };
        }
    }
}

[[nodiscard]] auto level_size(const int size, const unsigned int level) -> int
{
    return std::max(1, size >> level);
}

[[nodiscard]] auto solid_texels(const int width, const int height, const std::array<uint8_t, 4>& color) -> std::vector<uint8_t>
{
    std::vector<uint8_t> texels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t i = 0; i < texels.size(); i += 4u) {
        std::memcpy(texels.data() + i, color.data(), 4u);
    }
    return texels;
}

} // namespace

class Render_target_subresource_test : public Gpu_test
{
protected:
    // Copy one subresource (color aspect) to a mappable buffer; tightly packed
    // RGBA8 rows, row 0 at the device's texture origin.
    [[nodiscard]] auto read_subresource_rgba8(const erhe::graphics::Texture& texture, const Subresource& subresource) -> std::vector<uint8_t>
    {
        const int         width         = texture.get_width(subresource.level);
        const int         height        = texture.get_height(subresource.level);
        const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 4u;
        const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

        const std::shared_ptr<erhe::graphics::Buffer> readback = make_readback_buffer(byte_count, "read_subresource_rgba8");
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
                blit.copy_from_texture(
                    &texture,
                    static_cast<std::uintptr_t>(subresource.layer),     // source_slice
                    static_cast<std::uintptr_t>(subresource.level),     // source_level
                    glm::ivec3{0, 0, 0},                                // source_origin
                    glm::ivec3{width, height, 1},                       // source_size
                    readback.get(),                                     // destination_buffer
                    0,                                                  // destination_offset
                    static_cast<std::uintptr_t>(bytes_per_row),
                    static_cast<std::uintptr_t>(byte_count)
                );
            }
        );
        const std::vector<std::byte> raw = read_buffer(*readback, byte_count);
        std::vector<uint8_t> out(byte_count);
        std::memcpy(out.data(), raw.data(), byte_count);
        return out;
    }

    void seed_subresource(const erhe::graphics::Texture& texture, const Subresource& subresource, const std::vector<uint8_t>& texels)
    {
        const int         width         = texture.get_width(subresource.level);
        const int         height        = texture.get_height(subresource.level);
        const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 4u;
        const std::size_t byte_count    = texels.size();

        const std::shared_ptr<erhe::graphics::Buffer> source =
            make_host_buffer(byte_count, erhe::graphics::Buffer_usage::transfer_src, "subresource seed");
        {
            const std::span<std::byte> mapped = source->map_bytes(0, byte_count);
            std::memcpy(mapped.data(), texels.data(), byte_count);
            source->unmap();
        }
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(
                    source.get(),
                    0,                                                  // source_offset
                    static_cast<std::uintptr_t>(bytes_per_row),         // source_bytes_per_row
                    static_cast<std::uintptr_t>(byte_count),            // source_bytes_per_image
                    glm::ivec3{width, height, 1},                       // source_size
                    &texture,
                    static_cast<std::uintptr_t>(subresource.layer),     // destination_slice
                    static_cast<std::uintptr_t>(subresource.level),     // destination_level
                    glm::ivec3{0, 0, 0}                                 // destination_origin
                );
            }
        );
    }

    void run_case(const Target_kind kind, const Pass_kind pass, const char* golden_name)
    {
        erhe::graphics::Device& graphics_device = device();
        const Target_setup      setup           = make_target_setup(kind);

        const std::shared_ptr<erhe::graphics::Texture> texture = std::make_shared<erhe::graphics::Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info{
                .device            = graphics_device,
                .usage_mask        =
                    erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                    erhe::graphics::Image_usage_flag_bit_mask::sampled          |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_src     |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                .type              = setup.type,
                .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm,
                .width             = setup.size,
                .height            = setup.size,
                .array_layer_count = setup.array_layer_count,
                .level_count       = setup.level_count,
                .debug_label       = erhe::utility::Debug_label{setup.label}
            }
        );
        ASSERT_EQ(texture->get_level_count(), setup.level_count);
        ASSERT_EQ(texture->get_array_layer_count(), setup.array_layer_count);

        // Seed every subresource with its own solid color.
        std::vector<std::vector<uint8_t>> seeds;
        for (std::size_t i = 0; i < setup.subresources.size(); ++i) {
            const Subresource& subresource = setup.subresources[i];
            const int          size        = level_size(setup.size, subresource.level);
            seeds.push_back(solid_texels(size, size, c_seed_colors[i]));
            seed_subresource(*texture, subresource, seeds.back());
        }

        const Subresource& target      = setup.subresources[setup.target_index];
        const int          target_size = level_size(setup.size, target.level);

        const erhe::graphics::Bind_group_layout empty_layout{
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings          = {},
                .debug_label       = erhe::utility::Debug_label{"subresource target empty layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };
        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "render_target_subresource",
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
            },
            .bind_group_layout = &empty_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
        ASSERT_TRUE(prototype.is_valid()) << "subresource target shader failed to compile/link";
        erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = texture.get();
        descriptor.color_attachments[0].texture_level = target.level;
        descriptor.color_attachments[0].texture_layer = target.layer;
        descriptor.color_attachments[0].clear_value   = c_clear_value;
        descriptor.color_attachments[0].load_action   = (pass == Pass_kind::clear) ? erhe::graphics::Load_action::Clear : erhe::graphics::Load_action::Load;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::sampled;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::shader_read_only_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::shader_read_only_optimal;
        descriptor.render_target_width  = target_size;
        descriptor.render_target_height = target_size;
        descriptor.debug_label = erhe::utility::Debug_label{setup.label};

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
        ASSERT_TRUE(pipeline.is_valid()) << "subresource target pipeline is not valid";

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                if (pass == Pass_kind::load_and_draw) {
                    encoder.set_viewport_rect(0, 0, target_size, target_size);
                    encoder.set_scissor_rect (0, 0, target_size, target_size);
                    encoder.set_bind_group_layout(&empty_layout);
                    encoder.set_render_pipeline(pipeline);
                    encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
                }
            }
        );

        // Every untouched subresource still holds its seed, byte for byte.
        for (std::size_t i = 0; i < setup.subresources.size(); ++i) {
            if (i == setup.target_index) {
                continue;
            }
            const Subresource&         subresource = setup.subresources[i];
            const std::vector<uint8_t> pixels      = read_subresource_rgba8(*texture, subresource);
            ASSERT_EQ(pixels.size(), seeds[i].size());
            std::size_t differing = 0;
            for (std::size_t texel = 0; texel < pixels.size(); texel += 4u) {
                if (std::memcmp(pixels.data() + texel, seeds[i].data() + texel, 4u) != 0) {
                    ++differing;
                }
            }
            EXPECT_EQ(differing, 0u)
                << "layer " << subresource.layer << " level " << subresource.level
                << " (not the render target) changed: " << differing << " texels differ from its seed";
        }

        // The target subresource.
        const std::vector<uint8_t> pixels = read_subresource_rgba8(*texture, target);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(target_size) * static_cast<std::size_t>(target_size) * 4u);
        const std::vector<uint8_t>& target_seed = seeds[setup.target_index];
        auto texel_index = [target_size](const int x, const int y) -> std::size_t {
            return ((static_cast<std::size_t>(y) * static_cast<std::size_t>(target_size)) + static_cast<std::size_t>(x)) * 4u;
        };
        auto texel_near = [&pixels](const std::size_t index, const std::array<uint8_t, 4>& expected) -> bool {
            for (std::size_t c = 0; c < 4u; ++c) {
                if (std::abs(static_cast<int>(pixels[index + c]) - static_cast<int>(expected[c])) > 1) {
                    return false;
                }
            }
            return true;
        };
        if (pass == Pass_kind::clear) {
            int mismatches = 0;
            for (int y = 0; y < target_size; ++y) {
                for (int x = 0; x < target_size; ++x) {
                    if (!texel_near(texel_index(x, y), c_clear_bytes)) {
                        ++mismatches;
                    }
                }
            }
            EXPECT_EQ(mismatches, 0) << mismatches << " target texels differ from the clear color";
        } else {
            int mismatches = 0;
            for (int y = 0; y < target_size; ++y) {
                const bool corner_row = (y < c_corner) || (y >= (target_size - c_corner));
                for (int x = 0; x < target_size; ++x) {
                    const bool corner_column = (x < c_corner) || (x >= (target_size - c_corner));
                    if (!corner_row || !corner_column) {
                        continue;
                    }
                    const std::size_t index = texel_index(x, y);
                    if (std::memcmp(pixels.data() + index, target_seed.data() + index, 4u) != 0) {
                        ++mismatches;
                    }
                }
            }
            EXPECT_EQ(mismatches, 0) << mismatches << " target corner texels differ from the seed (Load did not preserve it)";
            EXPECT_TRUE(texel_near(texel_index(target_size / 2, target_size / 2), c_draw_bytes)) << "target centre texel is not the draw color";
        }
        expect_image_matches_golden(
            golden_name, target_size, target_size, erhe::dataformat::Format::format_8_vec4_unorm,
            std::as_bytes(std::span<const uint8_t>{pixels})
        );
    }
};

// agfx RenderPassClearMip: clear level 1 of a two-level 2D texture.
TEST_F(Render_target_subresource_test, clear_mip)
{
    run_case(Target_kind::mip, Pass_kind::clear, "render_target_clear_mip");
}

// agfx RenderPassClearSlice: clear layer 2 of a four-layer 2D array.
TEST_F(Render_target_subresource_test, clear_slice)
{
    run_case(Target_kind::slice, Pass_kind::clear, "render_target_clear_slice");
}

// agfx RenderPassClearFace: clear face 3 (-Y) of a cube map.
TEST_F(Render_target_subresource_test, clear_face)
{
    run_case(Target_kind::face, Pass_kind::clear, "render_target_clear_face");
}

// agfx RenderToTextureMip: load level 1 and draw a centred triangle into it.
TEST_F(Render_target_subresource_test, render_to_mip)
{
    run_case(Target_kind::mip, Pass_kind::load_and_draw, "render_target_draw_mip");
}

// agfx RenderToTextureSlice: load layer 2 and draw a centred triangle into it.
TEST_F(Render_target_subresource_test, render_to_slice)
{
    run_case(Target_kind::slice, Pass_kind::load_and_draw, "render_target_draw_slice");
}

// agfx RenderToTextureFace: load face 3 (-Y) and draw a centred triangle into it.
TEST_F(Render_target_subresource_test, render_to_face)
{
    run_case(Target_kind::face, Pass_kind::load_and_draw, "render_target_draw_face");
}

} // namespace erhe::graphics::test
