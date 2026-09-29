#include "gpu_test_fixture.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/math_util.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr int      c_ping_pong_size  = 16;
constexpr uint32_t c_round_trips     = 16;
constexpr int      c_hdr_size        = 64;

// UAVBarriers, buffer -> image: each texel becomes its buffer element + 1.
constexpr const char* c_buffer_to_image_source = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    ivec2 position = ivec2(gl_GlobalInvocationID.xy);
    uint  i        = (uint(position.y) * IMAGE_SIZE) + uint(position.x);
    float value    = float(Data.data[i]) + 1.0;
    imageStore(i_image, position, vec4(value, value, value, 1.0));
}
)glsl";

// UAVBarriers, image -> buffer: each buffer element becomes its texel + 1.
constexpr const char* c_image_to_buffer_source = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    ivec2 position = ivec2(gl_GlobalInvocationID.xy);
    uint  i        = (uint(position.y) * IMAGE_SIZE) + uint(position.x);
    Data.data[i] = uint(imageLoad(i_image, position).r) + 1u;
}
)glsl";

// WriteHDRTexture: values in [1, 9], all exactly representable, as a function
// of the image position (IMAGE_ROW maps the storage row to the image row, 0 =
// top), so one golden serves every backend.
constexpr const char* c_hdr_source = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    ivec2 position = ivec2(gl_GlobalInvocationID.xy);
    uint  x        = uint(position.x);
    uint  y        = IMAGE_ROW(uint(position.y));
    float red      = 1.0 + (float(x) * 0.125);
    float green    = 1.0 + (float(y) * 0.125);
    float blue     = 9.0 - (float((x ^ y) & 63u) * 0.125);
    imageStore(i_image, position, vec4(red, green, blue, 1.0));
}
)glsl";

enum class Layout_kind : unsigned int
{
    image_only,
    buffer_and_image
};

class Storage_image_test : public Gpu_test
{
protected:
    [[nodiscard]] auto make_storage_texture(const int size, const char* label) -> std::shared_ptr<erhe::graphics::Texture>
    {
        return std::make_shared<erhe::graphics::Texture>(
            device(),
            erhe::graphics::Texture_create_info{
                .device      = device(),
                .usage_mask  =
                    erhe::graphics::Image_usage_flag_bit_mask::storage |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_src,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_32_vec4_float,
                .width       = size,
                .height      = size,
                .debug_label = erhe::utility::Debug_label{label}
            }
        );
    }

    [[nodiscard]] auto make_layout(const Layout_kind kind, const char* label) -> std::unique_ptr<erhe::graphics::Bind_group_layout>
    {
        erhe::graphics::Bind_group_layout_create_info create_info{
            .debug_label       = erhe::utility::Debug_label{label},
            .uses_texture_heap = false
        };
        if (kind == Layout_kind::buffer_and_image) {
            create_info.bindings.push_back(
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = erhe::graphics::Binding_type::storage_buffer,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                }
            );
        }
        create_info.bindings.push_back(
            erhe::graphics::Bind_group_layout_binding{
                .binding_point = 1u,
                .type          = erhe::graphics::Binding_type::storage_image,
                .name          = "i_image",
                .glsl_type     = erhe::graphics::Glsl_type::image_2d,
                .image_format  = "rgba32f",
                .stage_flags   = erhe::graphics::Shader_stage_flags::compute
            }
        );
        return std::make_unique<erhe::graphics::Bind_group_layout>(device(), create_info);
    }
};

} // namespace

// agfx UAVBarriers: a buffer and an rgba32f storage image (set_storage_image,
// image in Image_layout::general) ping-pong 16 times, each step adding 1:
// buffer -> image, memory_barrier(shader_image_access), image -> buffer,
// memory_barrier(shader_storage). Every buffer element ends at 32 and every
// texel at 31. Buffer golden storage_image_ping_pong.bin.
TEST_F(Storage_image_test, uav_barriers)
{
    if (device().get_info().workaround_no_compute_storage_image_read) {
        GTEST_SKIP() << "the device cannot read a storage image in compute (Device_info::workaround_no_compute_storage_image_read)";
    }

    const std::unique_ptr<erhe::graphics::Bind_group_layout> layout = make_layout(Layout_kind::buffer_and_image, "storage image ping-pong layout");
    erhe::graphics::Shader_resource data_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Data",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    };
    data_block.add_uint("data", erhe::graphics::Shader_resource::unsized_array);
    const std::vector<std::pair<std::string, std::string>> defines{
        { "IMAGE_SIZE", std::to_string(c_ping_pong_size) + "u" }
    };
    const Compute_program to_image  = make_compute_program("storage_image_buffer_to_image", c_buffer_to_image_source, defines, {}, { &data_block }, *layout);
    const Compute_program to_buffer = make_compute_program("storage_image_image_to_buffer", c_image_to_buffer_source, defines, {}, { &data_block }, *layout);
    ASSERT_TRUE(to_image.is_valid());
    ASSERT_TRUE(to_buffer.is_valid());

    const std::size_t texel_count = static_cast<std::size_t>(c_ping_pong_size) * static_cast<std::size_t>(c_ping_pong_size);
    const std::size_t data_bytes  = texel_count * sizeof(uint32_t);
    const std::shared_ptr<erhe::graphics::Buffer> data = make_readback_buffer(data_bytes, "storage image ping-pong buffer");
    {
        const std::span<std::byte> mapped = data->map_bytes(0, data_bytes);
        std::memset(mapped.data(), 0, data_bytes);
        data->unmap();
    }
    const std::shared_ptr<erhe::graphics::Texture> image = make_storage_texture(c_ping_pong_size, "storage image ping-pong image");

    const uint32_t groups = static_cast<uint32_t>(c_ping_pong_size) / 8u;
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.transition_texture_layout(*image, erhe::graphics::Image_layout::general);
            for (uint32_t round_trip = 0; round_trip < c_round_trips; ++round_trip) {
                if (round_trip > 0) {
                    command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit);
                }
                {
                    erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                    encoder.set_bind_group_layout(layout.get());
                    encoder.set_compute_pipeline(*to_image.pipeline);
                    encoder.set_buffer(erhe::graphics::Buffer_target::storage, data.get(), 0, data_bytes, 0);
                    encoder.set_storage_image(1, *image);
                    encoder.dispatch_compute(groups, groups, 1);
                }
                command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::shader_image_access_barrier_bit);
                {
                    erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                    encoder.set_bind_group_layout(layout.get());
                    encoder.set_compute_pipeline(*to_buffer.pipeline);
                    encoder.set_buffer(erhe::graphics::Buffer_target::storage, data.get(), 0, data_bytes, 0);
                    encoder.set_storage_image(1, *image);
                    encoder.dispatch_compute(groups, groups, 1);
                }
            }
            // The texture readback copies the image out: make the image writes
            // visible to it (GL) and move the image to transfer_src_optimal.
            command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::texture_update_barrier_bit);
            command_buffer.transition_texture_layout(*image, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::vector<std::byte> raw = read_buffer(*data, data_bytes);
    ASSERT_EQ(raw.size(), data_bytes);
    std::vector<uint32_t> values(texel_count);
    std::memcpy(values.data(), raw.data(), data_bytes);
    int buffer_mismatches = 0;
    for (const uint32_t value : values) {
        if (value != (2u * c_round_trips)) {
            ++buffer_mismatches;
        }
    }
    EXPECT_EQ(buffer_mismatches, 0)
        << buffer_mismatches << " of " << texel_count << " buffer elements differ from " << (2u * c_round_trips)
        << "; element 0 = " << values[0];

    const std::vector<float> texels = read_texture_rgba32f(*image);
    ASSERT_EQ(texels.size(), texel_count * 4u);
    const float expected_texel = static_cast<float>((2u * c_round_trips) - 1u);
    int texel_mismatches = 0;
    for (std::size_t i = 0; i < texel_count; ++i) {
        if ((texels[(i * 4u) + 0u] != expected_texel) || (texels[(i * 4u) + 3u] != 1.0f)) {
            ++texel_mismatches;
        }
    }
    EXPECT_EQ(texel_mismatches, 0)
        << texel_mismatches << " of " << texel_count << " texels differ from " << expected_texel
        << "; texel 0 red = " << texels[0];

    expect_buffer_matches_golden("storage_image_ping_pong", std::span<const std::byte>{raw});
}

// agfx WriteHDRTexture: a compute shader writes values in [1, 9] into an
// rgba32f storage image (set_storage_image); the image is read back with
// read_texture_rgba32f, compared exactly against the CPU model and against the
// HDR-FLIP golden storage_image_hdr.pfm.
TEST_F(Storage_image_test, write_hdr_texture)
{
    const bool row0_is_top = (device().get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left);
    const std::unique_ptr<erhe::graphics::Bind_group_layout> layout = make_layout(Layout_kind::image_only, "storage image HDR layout");
    const Compute_program program = make_compute_program(
        "storage_image_hdr",
        c_hdr_source,
        { { "IMAGE_ROW(y)", row0_is_top ? std::string{"(y)"} : ("(" + std::to_string(c_hdr_size - 1) + "u - (y))") } },
        {},
        {},
        *layout
    );
    ASSERT_TRUE(program.is_valid());

    const std::shared_ptr<erhe::graphics::Texture> image = make_storage_texture(c_hdr_size, "storage image HDR");
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.transition_texture_layout(*image, erhe::graphics::Image_layout::general);
            {
                erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(layout.get());
                encoder.set_compute_pipeline(*program.pipeline);
                encoder.set_storage_image(1, *image);
                encoder.dispatch_compute(c_hdr_size / 8, c_hdr_size / 8, 1);
            }
            command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::texture_update_barrier_bit);
            command_buffer.transition_texture_layout(*image, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::size_t        texel_count = static_cast<std::size_t>(c_hdr_size) * static_cast<std::size_t>(c_hdr_size);
    const std::vector<float> texels      = read_texture_rgba32f(*image);
    ASSERT_EQ(texels.size(), texel_count * 4u);

    // CPU model in memory rows (the readback's row order).
    int         mismatches = 0;
    std::size_t first_bad  = 0;
    for (int row = 0; row < c_hdr_size; ++row) {
        const uint32_t y = row0_is_top ? static_cast<uint32_t>(row) : static_cast<uint32_t>(c_hdr_size - 1 - row);
        for (int column = 0; column < c_hdr_size; ++column) {
            const uint32_t    x    = static_cast<uint32_t>(column);
            const std::size_t base = ((static_cast<std::size_t>(row) * c_hdr_size) + static_cast<std::size_t>(column)) * 4u;
            const std::array<float, 4> expected{
                1.0f + (static_cast<float>(x) * 0.125f),
                1.0f + (static_cast<float>(y) * 0.125f),
                9.0f - (static_cast<float>((x ^ y) & 63u) * 0.125f),
                1.0f
            };
            for (std::size_t channel = 0; channel < 4; ++channel) {
                if (texels[base + channel] != expected[channel]) {
                    if (mismatches == 0) {
                        first_bad = base + channel;
                    }
                    ++mismatches;
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0)
        << mismatches << " of " << (texel_count * 4u) << " channels differ; first at texel " << (first_bad / 4u)
        << " channel " << (first_bad % 4u) << " = " << texels[first_bad];

    expect_image_matches_golden(
        "storage_image_hdr", c_hdr_size, c_hdr_size, erhe::dataformat::Format::format_32_vec4_float,
        std::as_bytes(std::span<const float>{texels})
    );
}

} // namespace erhe::graphics::test
