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
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// Reads "base" from a uniform block and writes base + i into the storage block.
constexpr const char* c_compute_source = R"glsl(
layout(local_size_x = 64) in;
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index < ELEMENT_COUNT) {
        Output.data[index] = Params.base + index;
    }
}
)glsl";

} // namespace

// Compute with a uniform-buffer input: exercises the uniform_buffer bind type,
// a UBO interface block in a compute shader, and binding both a UBO and an SSBO
// to one compute encoder.
TEST_F(Gpu_test, compute_uniform_buffer_input)
{
    const erhe::graphics::Device_info& info = device().get_info();
    if (!info.use_compute_shader || !info.use_shader_storage_buffers) {
        GTEST_SKIP() << "compute / storage buffers unavailable on this device";
    }

    constexpr uint32_t    element_count = 1000;
    constexpr uint32_t    base_value    = 5000;
    constexpr std::size_t ssbo_bytes    = static_cast<std::size_t>(element_count) * sizeof(uint32_t);
    constexpr std::size_t ubo_bytes     = 16; // std140 rounds a single-uint block up to 16

    erhe::graphics::Shader_resource ubo_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Params",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::uniform_block
        }
    };
    ubo_block.add_uint("base");

    erhe::graphics::Shader_resource ssbo_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Output",
            .binding_point = 1,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    };
    ssbo_block.add_uint("data", erhe::graphics::Shader_resource::unsized_array);

    const erhe::graphics::Bind_group_layout layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                { 0u, erhe::graphics::Binding_type::uniform_buffer },
                { 1u, erhe::graphics::Binding_type::storage_buffer }
            },
            .debug_label       = erhe::utility::Debug_label{"compute UBO layout"},
            .uses_texture_heap = false
        }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name              = "compute_ubo",
        .defines           = { { "ELEMENT_COUNT", "1000u" } },
        .interface_blocks  = { &ubo_block, &ssbo_block },
        .shaders           = { { erhe::graphics::Shader_type::compute_shader, std::string_view{c_compute_source} } },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "compute-UBO shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    const erhe::graphics::Compute_pipeline pipeline{
        device(),
        erhe::graphics::Compute_pipeline_data{
            .name              = "compute_ubo",
            .shader_stages     = &shader_stages,
            .bind_group_layout = &layout
        }
    };
    ASSERT_TRUE(pipeline.is_valid()) << "compute-UBO pipeline is not valid";

    const std::shared_ptr<erhe::graphics::Buffer> ubo =
        make_host_buffer(ubo_bytes, erhe::graphics::Buffer_usage::uniform, "compute UBO");
    const std::shared_ptr<erhe::graphics::Buffer> ssbo =
        make_host_buffer(ssbo_bytes, erhe::graphics::Buffer_usage::storage, "compute SSBO");
    {
        const std::span<std::byte> mapped = ubo->map_bytes(0, ubo_bytes);
        std::memcpy(mapped.data(), &base_value, sizeof(base_value));
        ubo->unmap();
    }

    const std::uintptr_t group_count = (element_count + 63u) / 64u;
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(&layout);
            encoder.set_compute_pipeline(pipeline);
            encoder.set_buffer(erhe::graphics::Buffer_target::uniform, ubo.get(),  0, ubo_bytes,  0);
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, ssbo.get(), 0, ssbo_bytes, 1);
            encoder.dispatch_compute(group_count, 1, 1);
        }
    );

    const std::vector<std::byte> raw = read_buffer(*ssbo, ssbo_bytes);
    std::vector<uint32_t> values(element_count);
    std::memcpy(values.data(), raw.data(), ssbo_bytes);

    int      mismatches      = 0;
    uint32_t first_bad_index = 0;
    for (uint32_t i = 0; i < element_count; ++i) {
        if (values[i] != (base_value + i)) {
            if (mismatches == 0) {
                first_bad_index = i;
            }
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0)
        << mismatches << " mismatches; first at index " << first_bad_index
        << " (got " << values[first_bad_index] << ", expected " << (base_value + first_bad_index) << ")";
}

} // namespace erhe::graphics::test
