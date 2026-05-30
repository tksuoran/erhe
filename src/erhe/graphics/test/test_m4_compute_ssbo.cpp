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

// erhe injects #version, the ELEMENT_COUNT define, and the SSBO block
// declaration (from the Shader_resource passed as an interface block). The
// block instance is named after its create_info name ("Output_buffer"), so the
// shader writes through Output_buffer.data[]. One element per global
// invocation; the bounds check exercises the tail (element_count is not a
// multiple of local_size_x).
constexpr const char* c_compute_source = R"glsl(
layout(local_size_x = 64) in;
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index < ELEMENT_COUNT) {
        Output_buffer.data[index] = index;
    }
}
)glsl";

} // namespace

// Milestone 4: dispatch a compute shader that writes data[i] = i into a storage
// buffer, read the buffer back, and assert exact integer equality for every
// element (no tolerance -- this is integer compute output).
TEST_F(Gpu_test, compute_writes_ssbo_pattern)
{
    const erhe::graphics::Device_info& info = device().get_info();
    if (!info.use_compute_shader) {
        GTEST_SKIP() << "compute shaders unavailable on this device";
    }
    if (!info.use_shader_storage_buffers) {
        GTEST_SKIP() << "shader storage buffers unavailable on this device";
    }

    constexpr uint32_t    element_count = 1000;          // not a multiple of 64
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
            .debug_label       = erhe::utility::Debug_label{"M4 compute layout"},
            .uses_texture_heap = false
        }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name              = "m4_compute_ssbo",
        .defines           = { { "ELEMENT_COUNT", "1000u" } },
        .interface_blocks  = { &ssbo_block },
        .shaders           = { { erhe::graphics::Shader_type::compute_shader, std::string_view{c_compute_source} } },
        .bind_group_layout = &compute_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "M4 compute shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    const erhe::graphics::Compute_pipeline pipeline{
        device(),
        erhe::graphics::Compute_pipeline_data{
            .name              = "m4_compute_ssbo",
            .shader_stages     = &shader_stages,
            .bind_group_layout = &compute_layout
        }
    };
    ASSERT_TRUE(pipeline.is_valid()) << "M4 compute pipeline is not valid";

    const std::shared_ptr<erhe::graphics::Buffer> output_buffer = make_readback_buffer(byte_count, "M4 ssbo");

    const std::uintptr_t group_count = (element_count + 63u) / 64u;
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(&compute_layout);
            encoder.set_compute_pipeline(pipeline);
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, output_buffer.get(), 0, byte_count, 0);
            encoder.dispatch_compute(group_count, 1, 1);
        }
    );

    const std::vector<std::byte> raw = read_buffer(*output_buffer, byte_count);
    ASSERT_EQ(raw.size(), byte_count);
    std::vector<uint32_t> values(element_count);
    std::memcpy(values.data(), raw.data(), byte_count);

    int      mismatches      = 0;
    uint32_t first_bad_index = 0;
    uint32_t first_bad_value = 0;
    for (uint32_t i = 0; i < element_count; ++i) {
        if (values[i] != i) {
            if (mismatches == 0) {
                first_bad_index = i;
                first_bad_value = values[i];
            }
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0)
        << "compute output mismatch: " << mismatches << " of " << element_count
        << " elements wrong; first at index " << first_bad_index
        << " (got " << first_bad_value << ", expected " << first_bad_index << ")";
}

} // namespace erhe::graphics::test
