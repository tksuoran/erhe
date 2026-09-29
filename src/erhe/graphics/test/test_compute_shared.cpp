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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr uint32_t c_group_size  = 64;
constexpr uint32_t c_group_count = 4;
constexpr uint32_t c_value_count = c_group_size * c_group_count;

// Each workgroup loads its 64 inputs into shared memory, and after a barrier
// every invocation reads the element mirrored within the group (a reverse,
// which only works if the other invocations' shared writes are visible). The
// reversed values go back into shared memory and a log-step tree reduction
// (stride 32, 16, ..., 1, a barrier after each step) sums the group; invocation
// 0 writes the group sum. Output: reversed values [0, 256), group sums at
// [256, 260).
constexpr const char* c_compute_source = R"glsl(
layout(local_size_x = GROUP_SIZE) in;
shared uint s_values[GROUP_SIZE];
void main()
{
    uint local_index  = gl_LocalInvocationID.x;
    uint global_index = gl_GlobalInvocationID.x;
    s_values[local_index] = Input.data[global_index];
    memoryBarrierShared();
    barrier();
    uint reversed = s_values[GROUP_SIZE - 1u - local_index];
    Output.data[global_index] = reversed;
    memoryBarrierShared();
    barrier();
    s_values[local_index] = reversed;
    memoryBarrierShared();
    barrier();
    for (uint stride = GROUP_SIZE / 2u; stride > 0u; stride = stride / 2u) {
        if (local_index < stride) {
            s_values[local_index] = s_values[local_index] + s_values[local_index + stride];
        }
        memoryBarrierShared();
        barrier();
    }
    if (local_index == 0u) {
        Output.data[VALUE_COUNT + gl_WorkGroupID.x] = s_values[0];
    }
}
)glsl";

} // namespace

// agfx ComputeSharedMemory: a `shared` array reversed within each of four
// workgroups, then a barriered log-step reduction per group. The output SSBO
// (reversed values followed by the four group sums) is compared against the
// CPU model and byte-exact against the buffer golden compute_shared_memory.bin.
TEST_F(Gpu_test, compute_shared_memory)
{
    erhe::graphics::Shader_resource input_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Input",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    };
    input_block.add_uint("data", erhe::graphics::Shader_resource::unsized_array);
    erhe::graphics::Shader_resource output_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Output",
            .binding_point = 1,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    };
    output_block.add_uint("data", erhe::graphics::Shader_resource::unsized_array);

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
                    .type          = erhe::graphics::Binding_type::storage_buffer,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"compute shared memory layout"},
            .uses_texture_heap = false
        }
    };

    const Compute_program program = make_compute_program(
        "compute_shared_memory",
        c_compute_source,
        {
            { "GROUP_SIZE",  std::to_string(c_group_size) + "u"  },
            { "VALUE_COUNT", std::to_string(c_value_count) + "u" }
        },
        {},
        { &input_block, &output_block },
        layout
    );
    ASSERT_TRUE(program.is_valid());

    std::vector<uint32_t> input(c_value_count);
    for (uint32_t i = 0; i < c_value_count; ++i) {
        input[i] = (i * 7u) + 3u + ((i * i) % 11u);
    }
    const std::size_t input_bytes  = input.size() * sizeof(uint32_t);
    const std::size_t output_count = c_value_count + c_group_count;
    const std::size_t output_bytes = output_count * sizeof(uint32_t);

    const std::shared_ptr<erhe::graphics::Buffer> input_buffer =
        make_host_buffer(input_bytes, erhe::graphics::Buffer_usage::storage, "compute shared memory input");
    {
        const std::span<std::byte> mapped = input_buffer->map_bytes(0, input_bytes);
        std::memcpy(mapped.data(), input.data(), input_bytes);
        input_buffer->unmap();
    }
    const std::shared_ptr<erhe::graphics::Buffer> output_buffer = make_readback_buffer(output_bytes, "compute shared memory output");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(&layout);
            encoder.set_compute_pipeline(*program.pipeline);
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, input_buffer.get(),  0, input_bytes,  0);
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, output_buffer.get(), 0, output_bytes, 1);
            encoder.dispatch_compute(c_group_count, 1, 1);
        }
    );

    std::vector<uint32_t> expected(output_count, 0u);
    for (uint32_t group = 0; group < c_group_count; ++group) {
        uint32_t sum = 0;
        for (uint32_t local = 0; local < c_group_size; ++local) {
            const uint32_t value = input[(group * c_group_size) + (c_group_size - 1u - local)];
            expected[(group * c_group_size) + local] = value;
            sum += value;
        }
        expected[c_value_count + group] = sum;
    }

    const std::vector<std::byte> raw = read_buffer(*output_buffer, output_bytes);
    ASSERT_EQ(raw.size(), output_bytes);
    std::vector<uint32_t> got(output_count);
    std::memcpy(got.data(), raw.data(), output_bytes);

    int         mismatches = 0;
    std::size_t first_bad  = 0;
    for (std::size_t i = 0; i < output_count; ++i) {
        if (got[i] != expected[i]) {
            if (mismatches == 0) {
                first_bad = i;
            }
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0)
        << mismatches << " of " << output_count << " words differ; first at " << first_bad
        << " (got " << got[first_bad] << ", expected " << expected[first_bad] << ")";
    for (uint32_t group = 0; group < c_group_count; ++group) {
        EXPECT_EQ(got[c_value_count + group], expected[c_value_count + group]) << "sum of group " << group;
    }

    expect_buffer_matches_golden("compute_shared_memory", std::span<const std::byte>{raw});
}

} // namespace erhe::graphics::test
