#include "gpu_test_fixture.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
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

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr uint32_t c_pass_count  = 4;
constexpr uint32_t c_group_size  = 64;
constexpr uint32_t c_value_count = 256;
constexpr int      c_image_size  = 64;

// Buffer variant: every pass reads and rewrites each element, folding in the
// pass index from the uniform block, so the result depends on every pass
// having seen the previous pass's writes.
constexpr const char* c_buffer_source = R"glsl(
layout(local_size_x = GROUP_SIZE) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    uint value = Data.data[i];
    value = ((value ^ (0x9E3779B9u * (Pass.index + 1u))) * 16777619u) + Pass.index + i;
    Data.data[i] = value;
}
)glsl";

// Texture variant: the SSBO holds IMAGE_SIZE x IMAGE_SIZE RGBA8 texels packed
// in uints (red in the low byte), rows in memory order: buffer row r is the
// texture row at distance r from the device's texture origin once the buffer
// is copied into the texture. IMAGE_ROW maps it to the image row (0 = top), so
// the picture is the same on every backend. Pass 0 writes red = 4 x, pass 1
// adds green = 4 image_y, pass 2 adds blue = red ^ green read back from the
// earlier passes' writes, pass 3 sets alpha to 255.
constexpr const char* c_texture_source = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    uint x     = gl_GlobalInvocationID.x;
    uint y     = gl_GlobalInvocationID.y;
    uint i     = (y * IMAGE_SIZE) + x;
    uint texel = Data.data[i];
    if (Pass.index == 0u) {
        texel = x * 4u;
    } else if (Pass.index == 1u) {
        texel = texel | ((IMAGE_ROW(y) * 4u) << 8u);
    } else if (Pass.index == 2u) {
        uint red   = texel & 0xFFu;
        uint green = (texel >> 8u) & 0xFFu;
        texel = texel | ((red ^ green) << 16u);
    } else {
        texel = texel | 0xFF000000u;
    }
    Data.data[i] = texel;
}
)glsl";

[[nodiscard]] auto buffer_pass(const uint32_t value, const uint32_t pass, const uint32_t index) -> uint32_t
{
    return ((value ^ (0x9E3779B9u * (pass + 1u))) * 16777619u) + pass + index;
}

class Multi_dispatch_test : public Gpu_test
{
protected:
    // Run c_pass_count dispatches of source over data_buffer (binding 0), each
    // with its pass index in a uniform block (binding 1, one aligned slot per
    // pass), with a shader storage memory_barrier between consecutive
    // dispatches. after_dispatches records more commands into the same
    // command buffer after the last dispatch.
    void run_passes(
        const char*                                                   name,
        std::string_view                                              source,
        const std::vector<std::pair<std::string, std::string>>&       defines,
        const erhe::graphics::Buffer&                                 data_buffer,
        std::size_t                                                   data_bytes,
        uint32_t                                                      group_count_x,
        uint32_t                                                      group_count_y,
        const std::function<void(erhe::graphics::Command_buffer&)>&   after_dispatches
    )
    {
        erhe::graphics::Shader_resource data_block{
            device(),
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "Data",
                .binding_point = 0,
                .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
            }
        };
        data_block.add_uint("data", erhe::graphics::Shader_resource::unsized_array);
        erhe::graphics::Shader_resource pass_block{
            device(),
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "Pass",
                .binding_point = 1,
                .type          = erhe::graphics::Shader_resource::Type::uniform_block
            }
        };
        const std::size_t index_offset = pass_block.add_uint("index")->get_offset_in_parent();
        const std::size_t pass_bytes   = pass_block.get_size_bytes();

        const erhe::graphics::Bind_group_layout layout{
            device(),
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 0u,
                        .type          = erhe::graphics::Binding_type::storage_buffer,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                    },
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 1u,
                        .type          = erhe::graphics::Binding_type::uniform_buffer,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                    }
                },
                .debug_label       = erhe::utility::Debug_label{"multi dispatch layout"},
                .uses_texture_heap = false
            }
        };
        const Compute_program program = make_compute_program(name, source, defines, {}, { &data_block, &pass_block }, layout);
        ASSERT_TRUE(program.is_valid());

        const std::size_t alignment = device().get_info().uniform_buffer_offset_alignment;
        const std::size_t stride    = ((pass_bytes + alignment - 1u) / alignment) * alignment;
        const std::shared_ptr<erhe::graphics::Buffer> pass_buffer =
            make_host_buffer(stride * c_pass_count, erhe::graphics::Buffer_usage::uniform, "multi dispatch pass indices");
        {
            const std::span<std::byte> mapped = pass_buffer->map_bytes(0, stride * c_pass_count);
            std::memset(mapped.data(), 0, mapped.size());
            for (uint32_t pass = 0; pass < c_pass_count; ++pass) {
                std::memcpy(mapped.data() + (pass * stride) + index_offset, &pass, sizeof(pass));
            }
            pass_buffer->unmap();
        }

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                for (uint32_t pass = 0; pass < c_pass_count; ++pass) {
                    if (pass > 0) {
                        command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit);
                    }
                    erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                    encoder.set_bind_group_layout(&layout);
                    encoder.set_compute_pipeline(*program.pipeline);
                    encoder.set_buffer(erhe::graphics::Buffer_target::storage, &data_buffer,       0,              data_bytes, 0);
                    encoder.set_buffer(erhe::graphics::Buffer_target::uniform, pass_buffer.get(), pass * stride,  pass_bytes, 1);
                    encoder.dispatch_compute(group_count_x, group_count_y, 1);
                }
                after_dispatches(command_buffer);
            }
        );
    }
};

} // namespace

// agfx ComputeMultiDispatchBuffer: four dispatches over one SSBO, each reading
// and rewriting every element with its pass index folded in, a memory_barrier
// between consecutive dispatches. CPU model plus the buffer golden
// compute_multi_dispatch_buffer.bin.
TEST_F(Multi_dispatch_test, buffer)
{
    std::vector<uint32_t> initial(c_value_count);
    for (uint32_t i = 0; i < c_value_count; ++i) {
        initial[i] = i * 0x01000193u;
    }
    const std::size_t data_bytes = initial.size() * sizeof(uint32_t);
    const std::shared_ptr<erhe::graphics::Buffer> data_buffer =
        make_host_buffer(data_bytes, erhe::graphics::Buffer_usage::storage, "multi dispatch data");
    {
        const std::span<std::byte> mapped = data_buffer->map_bytes(0, data_bytes);
        std::memcpy(mapped.data(), initial.data(), data_bytes);
        data_buffer->unmap();
    }

    run_passes(
        "compute_multi_dispatch_buffer",
        c_buffer_source,
        { { "GROUP_SIZE", std::to_string(c_group_size) + "u" } },
        *data_buffer,
        data_bytes,
        c_value_count / c_group_size,
        1,
        [](erhe::graphics::Command_buffer&) {}
    );
    if (HasFatalFailure()) {
        return;
    }

    std::vector<uint32_t> expected = initial;
    for (uint32_t pass = 0; pass < c_pass_count; ++pass) {
        for (uint32_t i = 0; i < c_value_count; ++i) {
            expected[i] = buffer_pass(expected[i], pass, i);
        }
    }

    const std::vector<std::byte> raw = read_buffer(*data_buffer, data_bytes);
    std::vector<uint32_t> got(c_value_count);
    std::memcpy(got.data(), raw.data(), data_bytes);
    int mismatches = 0;
    for (uint32_t i = 0; i < c_value_count; ++i) {
        if (got[i] != expected[i]) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << c_value_count << " elements differ from the CPU model";

    expect_buffer_matches_golden("compute_multi_dispatch_buffer", std::span<const std::byte>{raw});
}

// agfx ComputeMultiDispatchTexture: the four dispatches build an RGBA8 image in
// an SSBO (output routing: doc/plans/graphics_tests_agfx_port.md 2.3), which is
// then copied into a 64x64 texture with copy_from_buffer after a pixel buffer
// memory_barrier. The texture readback is compared texel-exact against the CPU
// model and against the image golden compute_multi_dispatch_texture.png.
TEST_F(Multi_dispatch_test, texture)
{
    const bool        row0_is_top = (device().get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left);
    const std::size_t texel_count = static_cast<std::size_t>(c_image_size) * static_cast<std::size_t>(c_image_size);
    const std::size_t data_bytes  = texel_count * sizeof(uint32_t);
    const std::shared_ptr<erhe::graphics::Buffer> data_buffer = make_host_buffer(
        data_bytes,
        erhe::graphics::Buffer_usage::storage | erhe::graphics::Buffer_usage::transfer_src,
        "multi dispatch texels"
    );
    {
        const std::span<std::byte> mapped = data_buffer->map_bytes(0, data_bytes);
        std::memset(mapped.data(), 0, data_bytes);
        data_buffer->unmap();
    }
    const std::shared_ptr<erhe::graphics::Texture> texture =
        make_sampled_texture(erhe::graphics::Texture_type::texture_2d, c_image_size, c_image_size, 1, 0, "multi dispatch texture");

    run_passes(
        "compute_multi_dispatch_texture",
        c_texture_source,
        {
            { "IMAGE_SIZE", std::to_string(c_image_size) + "u" },
            { "IMAGE_ROW(y)", row0_is_top ? std::string{"(y)"} : ("(" + std::to_string(c_image_size - 1) + "u - (y))") }
        },
        *data_buffer,
        data_bytes,
        c_image_size / 8,
        c_image_size / 8,
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::pixel_buffer_barrier_bit);
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(
                erhe::graphics::Buffer_texel_location{.buffer = data_buffer.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(c_image_size) * 4u, .bytes_per_image = static_cast<std::uintptr_t>(data_bytes)},
                glm::ivec3{c_image_size, c_image_size, 1},
                erhe::graphics::Texture_location{.texture = texture.get(), .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}}
            );
            command_buffer.transition_texture_layout(*texture, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );
    if (HasFatalFailure()) {
        return;
    }

    // CPU model in memory rows (the readback's row order).
    std::vector<uint8_t> expected(texel_count * 4u);
    for (int y = 0; y < c_image_size; ++y) {
        const uint32_t image_y = row0_is_top ? static_cast<uint32_t>(y) : static_cast<uint32_t>(c_image_size - 1 - y);
        for (int x = 0; x < c_image_size; ++x) {
            const uint32_t    red   = static_cast<uint32_t>(x) * 4u;
            const uint32_t    green = image_y * 4u;
            const std::size_t base  = ((static_cast<std::size_t>(y) * c_image_size) + static_cast<std::size_t>(x)) * 4u;
            expected[base + 0] = static_cast<uint8_t>(red);
            expected[base + 1] = static_cast<uint8_t>(green);
            expected[base + 2] = static_cast<uint8_t>(red ^ green);
            expected[base + 3] = 255u;
        }
    }

    const std::vector<uint8_t> pixels = read_texture_rgba8(*texture);
    ASSERT_EQ(pixels.size(), expected.size());
    expect_rgba8_near(pixels, expected, c_image_size, c_image_size, 0, "multi dispatch texture");

    expect_image_matches_golden(
        "compute_multi_dispatch_texture", c_image_size, c_image_size, erhe::dataformat::Format::format_8_vec4_unorm,
        std::as_bytes(std::span<const uint8_t>{pixels})
    );
}

} // namespace erhe::graphics::test
