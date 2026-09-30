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

#include <algorithm>
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

constexpr uint32_t c_thread_count     = 128;
constexpr uint32_t c_group_size       = 64;
constexpr uint32_t c_compare_initial  = 0x00001234u;
constexpr uint32_t c_compare_winner   = 77u;
constexpr uint32_t c_compare_tag      = 0xC0DE0000u;

// Every operand is chosen so the final value does not depend on the order in
// which the threads run: add counts, and / or / xor combine per-thread masks
// (commutative and associative), min / max see every thread id, and the
// compare-and-swap compare value matches the initial value for exactly one
// thread (COMPARE_WINNER), so exactly that thread swaps. Won.data[t] records
// whether thread t's compare matched the value it saw, which is deterministic
// too: only the winner's compare value is ever equal to the stored value.
constexpr const char* c_compute_source = R"glsl(
layout(local_size_x = GROUP_SIZE) in;
void main()
{
    uint t = gl_GlobalInvocationID.x;
    atomicAdd(Result.v_add, 1u);
    atomicAnd(Result.v_and, ~(1u << (t % 16u)));
    atomicOr (Result.v_or,  1u << (t % 24u));
    atomicXor(Result.v_xor, t * 0x9E3779B1u);
    atomicMin(Result.v_min, t + 1000u);
    atomicMax(Result.v_max, t * 3u);
    uint compare  = (t == COMPARE_WINNER) ? COMPARE_INITIAL : (COMPARE_INITIAL + 0x10000u);
    uint previous = atomicCompSwap(Result.v_cas, compare, COMPARE_TAG | t);
    Won.data[t] = (previous == compare) ? 1u : 0u;
}
)glsl";

} // namespace

// agfx ComputeBufferAtomics: 128 threads apply atomicAdd / And / Or / Xor /
// Min / Max / CompSwap to the members of one SSBO, and record in a second SSBO
// which thread won the compare-and-swap. The expected buffers are computed on
// the CPU and compared member by member, then byte-exact (the result block's
// members followed by the winner flags) against the buffer golden
// compute_atomics.bin.
TEST_F(Gpu_test, compute_buffer_atomics)
{
    erhe::graphics::Shader_resource result_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Result",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    };
    const std::size_t off_add = result_block.add_uint("v_add")->get_offset_in_parent();
    const std::size_t off_and = result_block.add_uint("v_and")->get_offset_in_parent();
    const std::size_t off_or  = result_block.add_uint("v_or" )->get_offset_in_parent();
    const std::size_t off_xor = result_block.add_uint("v_xor")->get_offset_in_parent();
    const std::size_t off_min = result_block.add_uint("v_min")->get_offset_in_parent();
    const std::size_t off_max = result_block.add_uint("v_max")->get_offset_in_parent();
    const std::size_t off_cas = result_block.add_uint("v_cas")->get_offset_in_parent();
    const std::size_t byte_count = result_block.get_size_bytes(erhe::graphics::Shader_resource::Layout::std430);
    // The block's reported size is padded to the device's buffer offset
    // alignment, so only the members reach the golden: the padding past v_cas
    // is as wide as the device wants it, and would make the golden device
    // specific.
    const std::size_t member_bytes = off_cas + sizeof(uint32_t);

    erhe::graphics::Shader_resource won_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Won",
            .binding_point = 1,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    };
    won_block.add_uint("data", erhe::graphics::Shader_resource::unsized_array);
    const std::size_t won_bytes = static_cast<std::size_t>(c_thread_count) * sizeof(uint32_t);

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
            .debug_label       = erhe::utility::Debug_label{"compute atomics layout"},
            .uses_texture_heap = false
        }
    };

    const Compute_program program = make_compute_program(
        "compute_atomics",
        c_compute_source,
        {
            { "GROUP_SIZE",      std::to_string(c_group_size) + "u"      },
            { "COMPARE_WINNER",  std::to_string(c_compare_winner) + "u"  },
            { "COMPARE_INITIAL", std::to_string(c_compare_initial) + "u" },
            { "COMPARE_TAG",     std::to_string(c_compare_tag) + "u"     }
        },
        {},
        { &result_block, &won_block },
        layout
    );
    ASSERT_TRUE(program.is_valid());

    // Initial values: the identity of each operation where it has one, so the
    // result is the combination of the thread operands alone.
    std::vector<uint32_t> initial(byte_count / sizeof(uint32_t), 0u);
    const auto word = [](std::vector<uint32_t>& words, const std::size_t offset) -> uint32_t& {
        return words[offset / sizeof(uint32_t)];
    };
    word(initial, off_add) = 0u;
    word(initial, off_and) = 0xFFFFFFFFu;
    word(initial, off_or ) = 0u;
    word(initial, off_xor) = 0u;
    word(initial, off_min) = 0xFFFFFFFFu;
    word(initial, off_max) = 0u;
    word(initial, off_cas) = c_compare_initial;

    const std::shared_ptr<erhe::graphics::Buffer> buffer =
        make_host_buffer(byte_count, erhe::graphics::Buffer_usage::storage, "compute atomics SSBO");
    {
        const std::span<std::byte> mapped = buffer->map_bytes(0, byte_count);
        std::memcpy(mapped.data(), initial.data(), byte_count);
        buffer->unmap();
    }
    const std::shared_ptr<erhe::graphics::Buffer> won_buffer = make_readback_buffer(won_bytes, "compute atomics winner flags");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(&layout);
            encoder.set_compute_pipeline(*program.pipeline);
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, buffer.get(),     0, byte_count, 0);
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, won_buffer.get(), 0, won_bytes,  1);
            encoder.dispatch_compute(c_thread_count / c_group_size, 1, 1);
        }
    );

    // CPU model of the same operations.
    std::vector<uint32_t> expected = initial;
    std::vector<uint32_t> expected_won(c_thread_count, 0u);
    for (uint32_t t = 0; t < c_thread_count; ++t) {
        word(expected, off_add) += 1u;
        word(expected, off_and) &= ~(1u << (t % 16u));
        word(expected, off_or ) |= (1u << (t % 24u));
        word(expected, off_xor) ^= (t * 0x9E3779B1u);
        word(expected, off_min) = std::min(word(expected, off_min), t + 1000u);
        word(expected, off_max) = std::max(word(expected, off_max), t * 3u);
        expected_won[t] = (t == c_compare_winner) ? 1u : 0u;
    }
    word(expected, off_cas) = c_compare_tag | c_compare_winner;

    const std::vector<std::byte> raw = read_buffer(*buffer, byte_count);
    ASSERT_EQ(raw.size(), byte_count);
    std::vector<uint32_t> got(byte_count / sizeof(uint32_t));
    std::memcpy(got.data(), raw.data(), byte_count);

    EXPECT_EQ(word(got, off_add), 128u);
    EXPECT_EQ(word(got, off_and), 0xFFFF0000u);
    EXPECT_EQ(word(got, off_or ), 0x00FFFFFFu);
    EXPECT_EQ(word(got, off_xor), word(expected, off_xor));
    EXPECT_EQ(word(got, off_min), 1000u);
    EXPECT_EQ(word(got, off_max), 381u);
    EXPECT_EQ(word(got, off_cas), c_compare_tag | c_compare_winner);
    EXPECT_TRUE(got == expected) << "atomics result block differs from the CPU model";

    const std::vector<std::byte> won_raw = read_buffer(*won_buffer, won_bytes);
    std::vector<uint32_t> won(c_thread_count);
    std::memcpy(won.data(), won_raw.data(), won_bytes);
    int won_mismatches = 0;
    for (uint32_t t = 0; t < c_thread_count; ++t) {
        if (won[t] != expected_won[t]) {
            ++won_mismatches;
        }
    }
    EXPECT_EQ(won_mismatches, 0) << "compare-and-swap winner flags differ from the CPU model (only thread " << c_compare_winner << " may win)";

    std::vector<std::byte> all_bytes{raw.begin(), raw.begin() + member_bytes};
    all_bytes.insert(all_bytes.end(), won_raw.begin(), won_raw.end());
    expect_buffer_matches_golden("compute_atomics", std::span<const std::byte>{all_bytes});
}

} // namespace erhe::graphics::test
