#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/buffer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace erhe::graphics::test {
namespace {

// Dispatch dimensions deliberately chosen so that:
//  - the work is 2D (group counts > 1 in both X and Y),
//  - the extents (37 x 21) are NOT multiples of the local work group size (8 x 8),
//    exercising the boundary handling of gl_GlobalInvocationID and the round-up of
//    group counts.
constexpr unsigned int extent_x      = 37u;
constexpr unsigned int extent_y      = 21u;
constexpr unsigned int local_size_x  = 8u;
constexpr unsigned int local_size_y  = 8u;

// The compute shader writes a value derived from the 2D global invocation id into a
// linear SSBO, guarded so out-of-range invocations (from the rounded-up dispatch) do
// not write. The packed value encodes both coordinates so the test can verify that
// every cell received its own id and nothing else clobbered it.
constexpr const char* compute_source =
    "layout(local_size_x = 8, local_size_y = 8) in;\n"
    "layout(std430, binding = 0) buffer Output_buffer {\n"
    "    uint values[];\n"
    "};\n"
    "void main() {\n"
    "    uint gx = gl_GlobalInvocationID.x;\n"
    "    uint gy = gl_GlobalInvocationID.y;\n"
    "    if ((gx >= 37u) || (gy >= 21u)) {\n"
    "        return;\n"
    "    }\n"
    "    uint index = (gy * 37u) + gx;\n"
    "    values[index] = (gy * 1000u) + gx;\n"
    "}\n";

} // namespace

TEST_F(Gpu_test, compute_2d_global_invocation_pattern)
{
    if (!device().get_info().use_compute_shader || !device().get_info().use_shader_storage_buffers) {
        GTEST_SKIP() << "compute/SSBO not supported";
    }

    erhe::graphics::Device& device = device();

    const unsigned int element_count = extent_x * extent_y;
    const std::size_t  byte_count    = static_cast<std::size_t>(element_count) * sizeof(uint32_t);

    erhe::graphics::Buffer_create_info storage_buffer_create_info{
        .device              = device,
        .capacity_byte_count = byte_count,
        .usage               = erhe::graphics::Buffer_usage::storage,
        .direction           = erhe::graphics::Buffer_direction::cpu_to_gpu,
        .access              = erhe::graphics::Buffer_access::map_write,
        .debug_label         = "compute 2d output buffer"
    };
    erhe::graphics::Buffer storage_buffer{storage_buffer_create_info};

    // Pre-fill with a sentinel so we can detect cells that were never written.
    {
        const std::byte* const_mapped = storage_buffer.map();
        std::byte* mapped = const_cast<std::byte*>(const_mapped);
        std::vector<uint32_t> sentinel(element_count, 0xffffffffu);
        std::memcpy(mapped, sentinel.data(), byte_count);
        storage_buffer.unmap();
    }

    erhe::graphics::Shader_stages_create_info shader_stages_create_info{
        .device = device,
        .pipeline_stages = {
            { erhe::graphics::Shader_type::compute_shader, compute_source }
        },
        .name = "compute 2d pipeline"
    };
    erhe::graphics::Shader_stages shader_stages{device, shader_stages_create_info};

    erhe::graphics::Compute_pipeline_state pipeline_state{
        erhe::graphics::Compute_pipeline_state::Create_info{
            .name          = "compute 2d pipeline",
            .shader_stages = &shader_stages
        }
    };

    // Round group counts up so that the whole extent is covered; this is what makes
    // the boundary guard in the shader necessary.
    const std::size_t group_count_x = (extent_x + local_size_x - 1u) / local_size_x;
    const std::size_t group_count_y = (extent_y + local_size_y - 1u) / local_size_y;
    ASSERT_GT(group_count_x, 1u);
    ASSERT_GT(group_count_y, 1u);

    erhe::graphics::Compute_command_encoder encoder = device.make_compute_command_encoder();
    encoder.set_compute_pipeline_state(pipeline_state);
    encoder.set_buffer(&storage_buffer, 0, byte_count, 0, erhe::graphics::Buffer_target::storage);
    encoder.dispatch_compute(group_count_x, group_count_y, 1);
    device.flush_compute_command_encoder(encoder);
    device.wait_for_idle();

    const std::byte* mapped = storage_buffer.map();
    std::vector<uint32_t> result(element_count);
    std::memcpy(result.data(), mapped, byte_count);
    storage_buffer.unmap();

    for (unsigned int gy = 0; gy < extent_y; ++gy) {
        for (unsigned int gx = 0; gx < extent_x; ++gx) {
            const unsigned int index    = (gy * extent_x) + gx;
            const uint32_t     expected = (gy * 1000u) + gx;
            EXPECT_EQ(result[index], expected) << "gx=" << gx << " gy=" << gy;
        }
    }
}

} // namespace erhe::graphics::test
