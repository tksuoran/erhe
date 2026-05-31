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
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr int c_dim = 2; // 2x2x2 volume

constexpr const char* c_3d_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)glsl";

// UVW are injected as defines so each pass fetches one fixed voxel center with
// nearest filtering, making the expected value exact.
constexpr const char* c_3d_fragment_source = R"glsl(
void main()
{
    out_color = texture(s_texture, vec3(U, V, W));
}
)glsl";

// Distinct RGBA8 voxel color from integer coordinates, varying along all three
// axes so the test exercises full 3D addressing rather than a single slice.
auto voxel_color(const int x, const int y, const int z) -> std::array<uint8_t, 4>
{
    return std::array<uint8_t, 4>{
        static_cast<uint8_t>(40 + (x * 90)),
        static_cast<uint8_t>(40 + (y * 90)),
        static_cast<uint8_t>(40 + (z * 90)),
        255u
    };
}

} // namespace

// 3D texture sampling. Create a 2x2x2 texture_3d, fill the whole volume in one
// copy_from_buffer (Z-major, tightly packed: source_size {2,2,2}), then in a
// pass per voxel sample the voxel center (UVW = (i+0.5)/2 per axis) through a
// sampler3D with nearest filtering and assert the fetched color equals the
// distinct color written to that voxel. set_sampled_image builds a
// VK_IMAGE_VIEW_TYPE_3D view, and the buffer->image copy carries the full depth
// extent (imageExtent.depth = source_size.z), so the W coordinate selects the
// correct slice.
TEST_F(Gpu_test, texture_3d_sample_voxels)
{
    constexpr int out_width  = 16;
    constexpr int out_height = 16;

    erhe::graphics::Device& graphics_device = device();

    // 3D texture: sampled + transfer_dst (copy target) + transfer_src (symmetry).
    const std::shared_ptr<erhe::graphics::Texture> volume = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::sampled      |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type        = erhe::graphics::Texture_type::texture_3d,
            .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
            .width       = c_dim,
            .height      = c_dim,
            .depth       = c_dim,
            .debug_label = erhe::utility::Debug_label{"volume texture"}
        }
    );
    ASSERT_EQ(volume->get_depth(),  c_dim);
    ASSERT_EQ(volume->get_width(),  c_dim);
    ASSERT_EQ(volume->get_height(), c_dim);

    // Host buffer holding the whole volume, Z-major (slice 0 rows, then slice 1
    // rows), tightly packed at c_dim*4 bytes per row and c_dim rows per slice.
    const std::size_t row_bytes    = static_cast<std::size_t>(c_dim) * 4u;
    const std::size_t slice_bytes  = row_bytes * static_cast<std::size_t>(c_dim);
    const std::size_t volume_bytes = slice_bytes * static_cast<std::size_t>(c_dim);
    std::vector<uint8_t> volume_data(volume_bytes);
    for (int z = 0; z < c_dim; ++z) {
        for (int y = 0; y < c_dim; ++y) {
            for (int x = 0; x < c_dim; ++x) {
                const std::array<uint8_t, 4> color = voxel_color(x, y, z);
                const std::size_t i =
                    (static_cast<std::size_t>(z) * slice_bytes) +
                    (static_cast<std::size_t>(y) * row_bytes) +
                    (static_cast<std::size_t>(x) * 4u);
                volume_data[i + 0] = color[0];
                volume_data[i + 1] = color[1];
                volume_data[i + 2] = color[2];
                volume_data[i + 3] = color[3];
            }
        }
    }

    const std::shared_ptr<erhe::graphics::Buffer> volume_buffer =
        make_host_buffer(volume_bytes, erhe::graphics::Buffer_usage::transfer_src, "volume source");
    {
        const std::span<std::byte> mapped = volume_buffer->map_bytes(0, volume_bytes);
        std::memcpy(mapped.data(), volume_data.data(), volume_bytes);
        volume_buffer->unmap();
    }

    // Single copy of the full 3D extent. source_bytes_per_image == one slice, so
    // the encoder derives bufferImageHeight = rows-per-slice; imageExtent.depth
    // comes from source_size.z. Leaves the image tracked in shader_read_only.
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(
                volume_buffer.get(),
                0,                                              // source_offset
                static_cast<std::uintptr_t>(row_bytes),         // source_bytes_per_row
                static_cast<std::uintptr_t>(slice_bytes),       // source_bytes_per_image (one slice)
                glm::ivec3{c_dim, c_dim, c_dim},                // source_size (full volume)
                volume.get(),
                0,                                              // destination_slice
                0,                                              // destination_level
                glm::ivec3{0, 0, 0}                             // destination_origin
            );
        }
    );

    const erhe::graphics::Sampler sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label = erhe::utility::Debug_label{"volume sample nearest"}
        }
    };

    const erhe::graphics::Bind_group_layout sampler_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0,
                    .type          = erhe::graphics::Binding_type::combined_image_sampler,
                    .name          = "s_texture",
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_3d
                }
            },
            .debug_label       = erhe::utility::Debug_label{"volume sample layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    // Voxel-center UVW string for a 2-wide axis: (i + 0.5) / 2 -> "0.25" / "0.75".
    const auto coord_str = [](const int i) -> std::string {
        return (i == 0) ? std::string{"0.25"} : std::string{"0.75"};
    };

    for (int z = 0; z < c_dim; ++z) {
        for (int y = 0; y < c_dim; ++y) {
            for (int x = 0; x < c_dim; ++x) {
                const std::array<uint8_t, 4>                   expected = voxel_color(x, y, z);
                const std::shared_ptr<erhe::graphics::Texture> output   = make_color_target(out_width, out_height);

                erhe::graphics::Shader_stages_create_info shader_create_info{
                    .name             = "texture_3d_sample",
                    .defines          = {
                        { "U", coord_str(x) },
                        { "V", coord_str(y) },
                        { "W", coord_str(z) }
                    },
                    .fragment_outputs = &fragment_outputs,
                    .no_vertex_input  = true,
                    .shaders = {
                        { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_3d_vertex_source}   },
                        { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_3d_fragment_source} }
                    },
                    .bind_group_layout = &sampler_layout
                };
                erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
                ASSERT_TRUE(prototype.is_valid()) << "3D-sample shader (voxel " << x << "," << y << "," << z << ") failed to compile/link";
                erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

                erhe::graphics::Render_pass_descriptor descriptor{};
                descriptor.color_attachments[0].texture       = output.get();
                descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
                descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
                descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
                descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
                descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
                descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
                descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
                descriptor.render_target_width  = out_width;
                descriptor.render_target_height = out_height;
                descriptor.debug_label = erhe::utility::Debug_label{"volume sample"};

                erhe::graphics::Render_pipeline_create_info pipeline_create_info;
                pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
                pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
                pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
                pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
                pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
                pipeline_create_info.base.bind_group_layout                 = &sampler_layout;
                pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
                pipeline_create_info.shader_stages                          = &shader_stages;
                pipeline_create_info.vertex_input                           = nullptr;
                pipeline_create_info.set_format_from_render_pass(descriptor);
                const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
                ASSERT_TRUE(pipeline.is_valid()) << "3D-sample pipeline (voxel " << x << "," << y << "," << z << ") is not valid";

                submit_and_wait(
                    [&](erhe::graphics::Command_buffer& command_buffer) {
                        erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
                        erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                        const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                        encoder.set_viewport_rect(0, 0, out_width, out_height);
                        encoder.set_scissor_rect (0, 0, out_width, out_height);
                        encoder.set_bind_group_layout(&sampler_layout);
                        encoder.set_render_pipeline(pipeline);
                        encoder.set_sampled_image(0, *volume, sampler);
                        encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
                    }
                );

                const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
                ASSERT_EQ(pixels.size(), static_cast<std::size_t>(out_width) * static_cast<std::size_t>(out_height) * 4u);

                // All texels in the output should equal the single fetched voxel.
                int bad = 0;
                for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
                    const bool ok =
                        (std::abs(static_cast<int>(pixels[i + 0]) - static_cast<int>(expected[0])) <= 2) &&
                        (std::abs(static_cast<int>(pixels[i + 1]) - static_cast<int>(expected[1])) <= 2) &&
                        (std::abs(static_cast<int>(pixels[i + 2]) - static_cast<int>(expected[2])) <= 2) &&
                        (std::abs(static_cast<int>(pixels[i + 3]) - static_cast<int>(expected[3])) <= 2);
                    if (!ok) {
                        ++bad;
                    }
                }
                EXPECT_EQ(bad, 0) << bad << " texels did not match voxel (" << x << "," << y << "," << z << ") color {"
                                  << static_cast<int>(expected[0]) << ","
                                  << static_cast<int>(expected[1]) << ","
                                  << static_cast<int>(expected[2]) << ","
                                  << static_cast<int>(expected[3]) << "}";
            }
        }
    }
}

} // namespace erhe::graphics::test
