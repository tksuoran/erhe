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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// The UBO carries an array of a user-defined struct (declared via
// Shader_resource::add_struct, listed in struct_types of every create_info
// that reads it). Each Item is:
//   vec4 a;     // offset  0
//   vec4 b;     // offset 16
//   uint s0;    // offset 32
//   uint s1;    // offset 36
//   uint s2;    // offset 40
//   uint s3;    // offset 44   -> struct size 48, std140-aligned (multiple of 16)
// The block is `Item items[ITEM_COUNT];` so the shader indexes
// Params.items[i].<field>. The compute shader reads several fields of each
// element and writes derived results into the SSBO; the test asserts the
// derived results exactly, which can only hold if every member was read at the
// correct std140 offset (a wrong offset shifts a field and the derived value
// changes).
//
// Result layout per item (RESULTS_PER_ITEM = 8 uints):
//   [0] = floatBitsToUint(a.x)
//   [1] = floatBitsToUint(a.w)
//   [2] = floatBitsToUint(b.y)
//   [3] = floatBitsToUint(b.z)
//   [4] = s0
//   [5] = s1
//   [6] = s2
//   [7] = s3
constexpr const char* c_compute_source = R"glsl(
layout(local_size_x = ITEM_COUNT) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= ITEM_COUNT) {
        return;
    }
    uint base = i * RESULTS_PER_ITEM;
    Output.data[base + 0u] = floatBitsToUint(Params.items[i].a.x);
    Output.data[base + 1u] = floatBitsToUint(Params.items[i].a.w);
    Output.data[base + 2u] = floatBitsToUint(Params.items[i].b.y);
    Output.data[base + 3u] = floatBitsToUint(Params.items[i].b.z);
    Output.data[base + 4u] = Params.items[i].s0;
    Output.data[base + 5u] = Params.items[i].s1;
    Output.data[base + 6u] = Params.items[i].s2;
    Output.data[base + 7u] = Params.items[i].s3;
}
)glsl";

constexpr uint32_t    item_count        = 2;
constexpr uint32_t    results_per_item  = 8;

// std140 host-side mirror of Item. The block contains item_count of these.
class Item
{
public:
    std::array<float, 4>    a;        //  0
    std::array<float, 4>    b;        // 16
    std::array<uint32_t, 4> s;        // 32 (s0..s3 at 32,36,40,44)
};
static_assert(sizeof(Item) == 48, "Item must be 48 bytes for std140");

} // namespace

// struct_types coverage: a compute shader reads several members of a
// user-defined struct embedded in a UBO (as an array), proving the GLSL
// struct declaration emitted from struct_types + the std140 member offsets
// are correct. The struct is referenced via Params.items[i].<field>; results
// are asserted bit-exact through the SSBO readback.
TEST_F(Gpu_test, struct_types_in_interface_block)
{
    const erhe::graphics::Device_info& info = device().get_info();
    if (!info.use_compute_shader || !info.use_shader_storage_buffers) {
        GTEST_SKIP() << "compute / storage buffers unavailable on this device";
    }

    // Struct type declaration: Item { vec4 a; vec4 b; uint s0..s3; }
    erhe::graphics::Shader_resource item_struct{device(), "Item"};
    const std::size_t off_a  = item_struct.add_vec4("a" )->get_offset_in_parent();
    const std::size_t off_b  = item_struct.add_vec4("b" )->get_offset_in_parent();
    const std::size_t off_s0 = item_struct.add_uint("s0")->get_offset_in_parent();
    const std::size_t off_s1 = item_struct.add_uint("s1")->get_offset_in_parent();
    const std::size_t off_s2 = item_struct.add_uint("s2")->get_offset_in_parent();
    const std::size_t off_s3 = item_struct.add_uint("s3")->get_offset_in_parent();

    // Cross-check the C++ Shader_resource offsets against the std140 layout the
    // host mirror assumes. If these diverge, the readback assertions below would
    // be meaningless, so assert them up front.
    ASSERT_EQ(off_a,   0u);
    ASSERT_EQ(off_b,  16u);
    ASSERT_EQ(off_s0, 32u);
    ASSERT_EQ(off_s1, 36u);
    ASSERT_EQ(off_s2, 40u);
    ASSERT_EQ(off_s3, 44u);
    ASSERT_EQ(item_struct.get_size_bytes(erhe::graphics::Shader_resource::Layout::std140), 48u);

    // UBO block: Item items[item_count];
    erhe::graphics::Shader_resource ubo_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Params",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::uniform_block
        }
    };
    ubo_block.add_struct("items", &item_struct, static_cast<std::size_t>(item_count));

    // SSBO output: uint data[];
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
            .debug_label       = erhe::utility::Debug_label{"struct_types layout"},
            .uses_texture_heap = false
        }
    };

    // The struct type MUST be listed in struct_types so its GLSL declaration is
    // emitted before the block that references it; the block goes in
    // interface_blocks.
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name              = "struct_types",
        .defines           = {
            { "ITEM_COUNT",       "2u" },
            { "RESULTS_PER_ITEM", "8u" }
        },
        .struct_types      = { &item_struct },
        .interface_blocks  = { &ubo_block, &ssbo_block },
        .shaders           = { { erhe::graphics::Shader_type::compute_shader, std::string_view{c_compute_source} } },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "struct_types compute shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    const erhe::graphics::Compute_pipeline pipeline{
        device(),
        erhe::graphics::Compute_pipeline_data{
            .name              = "struct_types",
            .shader_stages     = &shader_stages,
            .bind_group_layout = &layout
        }
    };
    ASSERT_TRUE(pipeline.is_valid()) << "struct_types compute pipeline is not valid";

    // Host-side UBO contents: distinct values per field and per element so that
    // any offset error produces a detectable mismatch.
    std::array<Item, item_count> items{};
    items[0].a = { 1.5f, 2.5f, 3.5f, 4.5f };
    items[0].b = { 10.5f, 11.5f, 12.5f, 13.5f };
    items[0].s = { 100u, 101u, 102u, 103u };
    items[1].a = { -1.0f, -2.0f, -3.0f, -4.0f };
    items[1].b = { 20.25f, 21.25f, 22.25f, 23.25f };
    items[1].s = { 200u, 201u, 202u, 203u };

    const std::size_t ubo_bytes  = sizeof(Item) * item_count;
    const std::size_t ssbo_count = static_cast<std::size_t>(item_count) * results_per_item;
    const std::size_t ssbo_bytes = ssbo_count * sizeof(uint32_t);

    const std::shared_ptr<erhe::graphics::Buffer> ubo =
        make_host_buffer(ubo_bytes, erhe::graphics::Buffer_usage::uniform, "struct_types UBO");
    const std::shared_ptr<erhe::graphics::Buffer> ssbo = make_readback_buffer(ssbo_bytes, "struct_types SSBO");
    {
        const std::span<std::byte> mapped = ubo->map_bytes(0, ubo_bytes);
        std::memcpy(mapped.data(), items.data(), ubo_bytes);
        ubo->unmap();
    }

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(&layout);
            encoder.set_compute_pipeline(pipeline);
            encoder.set_buffer(erhe::graphics::Buffer_target::uniform, ubo.get(),  0, ubo_bytes,  0);
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, ssbo.get(), 0, ssbo_bytes, 1);
            encoder.dispatch_compute(1, 1, 1);
        }
    );

    const std::vector<std::byte> raw = read_buffer(*ssbo, ssbo_bytes);
    ASSERT_EQ(raw.size(), ssbo_bytes);
    std::vector<uint32_t> got(ssbo_count);
    std::memcpy(got.data(), raw.data(), ssbo_bytes);

    auto bits = [](float f) -> uint32_t {
        uint32_t u = 0;
        std::memcpy(&u, &f, sizeof(u));
        return u;
    };

    for (uint32_t i = 0; i < item_count; ++i) {
        const Item&    item = items[i];
        const uint32_t base = i * results_per_item;
        EXPECT_EQ(got[base + 0u], bits(item.a[0])) << "item " << i << " a.x";
        EXPECT_EQ(got[base + 1u], bits(item.a[3])) << "item " << i << " a.w";
        EXPECT_EQ(got[base + 2u], bits(item.b[1])) << "item " << i << " b.y";
        EXPECT_EQ(got[base + 3u], bits(item.b[2])) << "item " << i << " b.z";
        EXPECT_EQ(got[base + 4u], item.s[0])       << "item " << i << " s0";
        EXPECT_EQ(got[base + 5u], item.s[1])       << "item " << i << " s1";
        EXPECT_EQ(got[base + 6u], item.s[2])       << "item " << i << " s2";
        EXPECT_EQ(got[base + 7u], item.s[3])       << "item " << i << " s3";
    }
}

} // namespace erhe::graphics::test
