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

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// 2D dispatch. The work group is 8x8; the extents 37x21 are deliberately NOT a
// multiple of the local size so the dispatch must round the group counts up
// (5x3 groups -> covers 40x24) and the shader must guard the out-of-range tail.
// Each in-range invocation writes a value derived from its 2D global id into a
// row-major SSBO; the test asserts every cell holds exactly its own id and the
// rounded-up tail invocations did not clobber anything.
//
// As in the SSBO milestone test, erhe injects #version, the EXTENT_* defines, and
// the SSBO block declaration (from the Shader_resource), so the shader writes
// through Output_buffer.data[].
constexpr const char* c_compute_source = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    uint gx = gl_GlobalInvocationID.x;
    uint gy = gl_GlobalInvocationID.y;
    if ((gx >= EXTENT_X) || (gy >= EXTENT_Y)) {
        return;
    }
    uint index = (gy * EXTENT_X) + gx;
    Output_buffer.data[index] = (gy * 1000u) + gx;
}
)glsl";

} // namespace

// Compute coverage: a 2D dispatch with group counts > 1 in both dimensions over
// extents that are not a multiple of the local work group size. Verifies the 2D
// gl_GlobalInvocationID mapping and the boundary guard on the rounded-up tail.
TEST_F(Gpu_test, compute_2d_global_invocation_pattern)
{
    const erhe::graphics::Device_info& info = device().get_info();
    if (!info.use_compute_shader) {
        GTEST_SKIP() << "compute shaders unavailable on this device";
    }
    if (!info.use_shader_storage_buffers) {
        GTEST_SKIP() << "shader storage buffers unavailable on this device";
    }

    constexpr uint32_t    extent_x      = 37u;
    constexpr uint32_t    extent_y      = 21u;
    constexpr uint32_t    local_size_x  = 8u;
    constexpr uint32_t    local_size_y  = 8u;
    constexpr uint32_t    element_count = extent_x * extent_y;
    constexpr std::size_t byte_count    = static_cast<std::size_t>(element_count) * sizeof(uint32_t);

    // SSBO: writeonly buffer { uint data[]; } Output_buffer; at binding 0.
    erhe::graphics::Shader_resource ssbo_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Output_buffer",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    };
    ssbo_block.add_uint("data", erhe::graphics::Shader_resource::unsized_array);

    const erhe::graphics::Bind_group_layout compute_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = { { 0u, erhe::graphics::Binding_type::storage_buffer } },
            .debug_label       = erhe::utility::Debug_label{"compute 2d layout"},
            .uses_texture_heap = false
        }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name              = "compute_2d",
        .defines           = { { "EXTENT_X", "37u" }, { "EXTENT_Y", "21u" } },
        .interface_blocks  = { &ssbo_block },
        .shaders           = { { erhe::graphics::Shader_type::compute_shader, std::string_view{c_compute_source} } },
        .bind_group_layout = &compute_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "compute 2d shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    const erhe::graphics::Compute_pipeline pipeline{
        device(),
        erhe::graphics::Compute_pipeline_data{
            .name              = "compute_2d",
            .shader_stages     = &shader_stages,
            .bind_group_layout = &compute_layout
        }
    };
    ASSERT_TRUE(pipeline.is_valid()) << "compute 2d pipeline is not valid";

    const std::shared_ptr<erhe::graphics::Buffer> output_buffer = make_readback_buffer(byte_count, "compute 2d ssbo");

    // Round group counts up so the whole extent is covered. This is what makes the
    // boundary guard in the shader necessary and exercises group counts > 1 per dim.
    const std::uintptr_t group_count_x = (extent_x + local_size_x - 1u) / local_size_x;
    const std::uintptr_t group_count_y = (extent_y + local_size_y - 1u) / local_size_y;
    ASSERT_GT(group_count_x, std::uintptr_t{1});
    ASSERT_GT(group_count_y, std::uintptr_t{1});

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(&compute_layout);
            encoder.set_compute_pipeline(pipeline);
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, output_buffer.get(), 0, byte_count, 0);
            encoder.dispatch_compute(group_count_x, group_count_y, 1);
        }
    );

    const std::vector<std::byte> raw = read_buffer(*output_buffer, byte_count);
    ASSERT_EQ(raw.size(), byte_count);
    std::vector<uint32_t> values(element_count);
    std::memcpy(values.data(), raw.data(), byte_count);

    int      mismatches      = 0;
    uint32_t first_bad_index = 0;
    uint32_t first_bad_value = 0;
    for (uint32_t gy = 0; gy < extent_y; ++gy) {
        for (uint32_t gx = 0; gx < extent_x; ++gx) {
            const uint32_t index    = (gy * extent_x) + gx;
            const uint32_t expected = (gy * 1000u) + gx;
            if (values[index] != expected) {
                if (mismatches == 0) {
                    first_bad_index = index;
                    first_bad_value = values[index];
                }
                ++mismatches;
            }
        }
    }
    EXPECT_EQ(mismatches, 0)
        << "2D compute output mismatch: " << mismatches << " of " << element_count
        << " cells wrong; first at linear index " << first_bad_index
        << " (got " << first_bad_value << ")";
}

} // namespace erhe::graphics::test
