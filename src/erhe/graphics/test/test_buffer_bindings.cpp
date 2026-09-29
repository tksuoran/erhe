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
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr uint32_t c_group_size    = 64;
constexpr uint32_t c_element_count = 256;
constexpr uint32_t c_constant_out  = 64;

// --- BufferViewByteAddress: a raw uint[] SSBO. The writer fills it with a
// hash of the index; the reader folds each element with another element that
// a different invocation of the writer dispatch wrote.
constexpr const char* c_raw_writer_source = R"glsl(
layout(local_size_x = GROUP_SIZE) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    Raw.data[i] = (i * 2654435761u) ^ 0x5BD1E995u;
}
)glsl";

constexpr const char* c_raw_reader_source = R"glsl(
layout(local_size_x = GROUP_SIZE) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    Output.data[i] = Raw.data[i] + Raw.data[((i * 7u) + 1u) % ELEMENT_COUNT];
}
)glsl";

[[nodiscard]] auto raw_value(const uint32_t i) -> uint32_t
{
    return (i * 2654435761u) ^ 0x5BD1E995u;
}

// --- BufferViewStructured: an SSBO array of a 16-byte struct. The reader
// combines the mirrored element's members with this element's tag.
constexpr const char* c_structured_writer_source = R"glsl(
layout(local_size_x = GROUP_SIZE) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    Elements.elements[i].id     = i;
    Elements.elements[i].weight = float(i) * 0.5;
    Elements.elements[i].mask   = 1u << (i % 32u);
    Elements.elements[i].tag    = 0xABC00000u | i;
}
)glsl";

constexpr const char* c_structured_reader_source = R"glsl(
layout(local_size_x = GROUP_SIZE) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    uint j = ELEMENT_COUNT - 1u - i;
    Output.data[i] =
        Elements.elements[j].id +
        (uint(Elements.elements[j].weight * 2.0) * 3u) +
        (Elements.elements[j].mask ^ Elements.elements[i].tag);
}
)glsl";

// --- BufferViewConstant: a uniform block with a vec4 after three uints (std140
// offsets 0, 4, 8, 16). The writer fills the buffer through a uint[] storage
// view at the word offsets the Shader_resource reports; the reader reads the
// same buffer as the uniform block.
constexpr const char* c_constant_writer_source = R"glsl(
layout(local_size_x = 1) in;
void main()
{
    Raw.data[WORD_A]      = 1000u;
    Raw.data[WORD_B]      = 0x5A5Au;
    Raw.data[WORD_C]      = 77u;
    Raw.data[WORD_V + 0u] = floatBitsToUint(1.5);
    Raw.data[WORD_V + 1u] = floatBitsToUint(2.25);
    Raw.data[WORD_V + 2u] = floatBitsToUint(3.125);
    Raw.data[WORD_V + 3u] = floatBitsToUint(4.0);
}
)glsl";

constexpr const char* c_constant_reader_source = R"glsl(
layout(local_size_x = GROUP_SIZE) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    Output.data[i] = (Params.a * i) + (Params.b ^ i) + Params.c + uint(Params.v[i % 4u] * 8.0);
}
)glsl";

// One buffer binding of a dispatch.
class Dispatch_binding
{
public:
    erhe::graphics::Buffer_target  target;
    const erhe::graphics::Buffer*  buffer;
    std::size_t                    byte_count;
    uint32_t                       binding_point;
};

[[nodiscard]] auto make_ssbo_block(
    erhe::graphics::Device& device,
    const char*             name,
    const int               binding_point
) -> std::unique_ptr<erhe::graphics::Shader_resource>
{
    return std::make_unique<erhe::graphics::Shader_resource>(
        device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = name,
            .binding_point = binding_point,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    );
}

class Buffer_binding_test : public Gpu_test
{
protected:
    // Writer dispatch, memory_barrier(barrier), reader dispatch, in one
    // command buffer.
    void write_then_read(
        const erhe::graphics::Bind_group_layout&  layout,
        const Compute_program&                    writer,
        std::span<const Dispatch_binding>         writer_bindings,
        uint32_t                                  writer_groups,
        erhe::graphics::Memory_barrier_mask       barrier,
        const Compute_program&                    reader,
        std::span<const Dispatch_binding>         reader_bindings,
        uint32_t                                  reader_groups
    )
    {
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                {
                    erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                    encoder.set_bind_group_layout(&layout);
                    encoder.set_compute_pipeline(*writer.pipeline);
                    for (const Dispatch_binding& binding : writer_bindings) {
                        encoder.set_buffer(binding.target, binding.buffer, 0, binding.byte_count, binding.binding_point);
                    }
                    encoder.dispatch_compute(writer_groups, 1, 1);
                }
                command_buffer.memory_barrier(barrier);
                {
                    erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                    encoder.set_bind_group_layout(&layout);
                    encoder.set_compute_pipeline(*reader.pipeline);
                    for (const Dispatch_binding& binding : reader_bindings) {
                        encoder.set_buffer(binding.target, binding.buffer, 0, binding.byte_count, binding.binding_point);
                    }
                    encoder.dispatch_compute(reader_groups, 1, 1);
                }
            }
        );
    }

    [[nodiscard]] auto make_layout(const std::span<const erhe::graphics::Binding_type> types, const char* label)
        -> std::unique_ptr<erhe::graphics::Bind_group_layout>
    {
        erhe::graphics::Bind_group_layout_create_info create_info{
            .debug_label       = erhe::utility::Debug_label{label},
            .uses_texture_heap = false
        };
        for (std::size_t i = 0; i < types.size(); ++i) {
            create_info.bindings.push_back(
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = static_cast<uint32_t>(i),
                    .type          = types[i],
                    .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                }
            );
        }
        return std::make_unique<erhe::graphics::Bind_group_layout>(device(), create_info);
    }

    // Compare the output words against the CPU model, then byte-exact against
    // the buffer golden.
    void expect_output(erhe::graphics::Buffer& output, const std::vector<uint32_t>& expected, const char* golden_name)
    {
        const std::size_t            byte_count = expected.size() * sizeof(uint32_t);
        const std::vector<std::byte> raw        = read_buffer(output, byte_count);
        ASSERT_EQ(raw.size(), byte_count);
        std::vector<uint32_t> got(expected.size());
        std::memcpy(got.data(), raw.data(), byte_count);
        int         mismatches = 0;
        std::size_t first_bad  = 0;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (got[i] != expected[i]) {
                if (mismatches == 0) {
                    first_bad = i;
                }
                ++mismatches;
            }
        }
        EXPECT_EQ(mismatches, 0)
            << golden_name << ": " << mismatches << " of " << expected.size() << " words differ; first at " << first_bad
            << " (got " << got[first_bad] << ", expected " << expected[first_bad] << ")";
        expect_buffer_matches_golden(golden_name, std::span<const std::byte>{raw});
    }
};

} // namespace

// agfx BufferViewByteAddress: raw uint[] SSBO written by one dispatch and read
// by the next (reads cross invocations of the writer), folded into an output
// SSBO; buffer golden buffer_view_byte_address.bin.
TEST_F(Buffer_binding_test, byte_address)
{
    const std::unique_ptr<erhe::graphics::Shader_resource> raw_block    = make_ssbo_block(device(), "Raw", 0);
    const std::unique_ptr<erhe::graphics::Shader_resource> output_block = make_ssbo_block(device(), "Output", 1);
    raw_block   ->add_uint("data", erhe::graphics::Shader_resource::unsized_array);
    output_block->add_uint("data", erhe::graphics::Shader_resource::unsized_array);

    const std::array<erhe::graphics::Binding_type, 2> types{
        erhe::graphics::Binding_type::storage_buffer,
        erhe::graphics::Binding_type::storage_buffer
    };
    const std::unique_ptr<erhe::graphics::Bind_group_layout> layout = make_layout(types, "byte address layout");
    const std::vector<std::pair<std::string, std::string>> defines{
        { "GROUP_SIZE",    std::to_string(c_group_size) + "u"    },
        { "ELEMENT_COUNT", std::to_string(c_element_count) + "u" }
    };
    const Compute_program writer = make_compute_program("byte_address_writer", c_raw_writer_source, defines, {}, { raw_block.get() }, *layout);
    const Compute_program reader = make_compute_program("byte_address_reader", c_raw_reader_source, defines, {}, { raw_block.get(), output_block.get() }, *layout);
    ASSERT_TRUE(writer.is_valid());
    ASSERT_TRUE(reader.is_valid());

    const std::size_t bytes = static_cast<std::size_t>(c_element_count) * sizeof(uint32_t);
    const std::shared_ptr<erhe::graphics::Buffer> raw    = make_readback_buffer(bytes, "byte address data");
    const std::shared_ptr<erhe::graphics::Buffer> output = make_readback_buffer(bytes, "byte address output");
    const std::array<Dispatch_binding, 1> writer_bindings{
        Dispatch_binding{ erhe::graphics::Buffer_target::storage, raw.get(), bytes, 0 }
    };
    const std::array<Dispatch_binding, 2> reader_bindings{
        Dispatch_binding{ erhe::graphics::Buffer_target::storage, raw.get(),    bytes, 0 },
        Dispatch_binding{ erhe::graphics::Buffer_target::storage, output.get(), bytes, 1 }
    };
    write_then_read(
        *layout,
        writer, writer_bindings, c_element_count / c_group_size,
        erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit,
        reader, reader_bindings, c_element_count / c_group_size
    );

    std::vector<uint32_t> expected(c_element_count);
    for (uint32_t i = 0; i < c_element_count; ++i) {
        expected[i] = raw_value(i) + raw_value(((i * 7u) + 1u) % c_element_count);
    }
    expect_output(*output, expected, "buffer_view_byte_address");
}

// agfx BufferViewStructured: an SSBO array of a 16-byte struct (add_struct +
// struct_types), written by one dispatch and read by the next; buffer golden
// buffer_view_structured.bin.
TEST_F(Buffer_binding_test, structured)
{
    erhe::graphics::Shader_resource element_struct{device(), "Element"};
    element_struct.add_uint ("id"    );
    element_struct.add_float("weight");
    element_struct.add_uint ("mask"  );
    element_struct.add_uint ("tag"   );
    ASSERT_EQ(element_struct.get_size_bytes(erhe::graphics::Shader_resource::Layout::std430), 16u);

    const std::unique_ptr<erhe::graphics::Shader_resource> elements_block = make_ssbo_block(device(), "Elements", 0);
    const std::unique_ptr<erhe::graphics::Shader_resource> output_block   = make_ssbo_block(device(), "Output", 1);
    elements_block->add_struct("elements", &element_struct, erhe::graphics::Shader_resource::unsized_array);
    output_block  ->add_uint("data", erhe::graphics::Shader_resource::unsized_array);

    const std::array<erhe::graphics::Binding_type, 2> types{
        erhe::graphics::Binding_type::storage_buffer,
        erhe::graphics::Binding_type::storage_buffer
    };
    const std::unique_ptr<erhe::graphics::Bind_group_layout> layout = make_layout(types, "structured layout");
    const std::vector<std::pair<std::string, std::string>> defines{
        { "GROUP_SIZE",    std::to_string(c_group_size) + "u"    },
        { "ELEMENT_COUNT", std::to_string(c_element_count) + "u" }
    };
    const Compute_program writer = make_compute_program(
        "structured_writer", c_structured_writer_source, defines, { &element_struct }, { elements_block.get() }, *layout
    );
    const Compute_program reader = make_compute_program(
        "structured_reader", c_structured_reader_source, defines, { &element_struct }, { elements_block.get(), output_block.get() }, *layout
    );
    ASSERT_TRUE(writer.is_valid());
    ASSERT_TRUE(reader.is_valid());

    const std::size_t element_bytes = static_cast<std::size_t>(c_element_count) * 16u;
    const std::size_t output_bytes  = static_cast<std::size_t>(c_element_count) * sizeof(uint32_t);
    const std::shared_ptr<erhe::graphics::Buffer> elements = make_readback_buffer(element_bytes, "structured elements");
    const std::shared_ptr<erhe::graphics::Buffer> output   = make_readback_buffer(output_bytes,  "structured output");
    const std::array<Dispatch_binding, 1> writer_bindings{
        Dispatch_binding{ erhe::graphics::Buffer_target::storage, elements.get(), element_bytes, 0 }
    };
    const std::array<Dispatch_binding, 2> reader_bindings{
        Dispatch_binding{ erhe::graphics::Buffer_target::storage, elements.get(), element_bytes, 0 },
        Dispatch_binding{ erhe::graphics::Buffer_target::storage, output.get(),   output_bytes,  1 }
    };
    write_then_read(
        *layout,
        writer, writer_bindings, c_element_count / c_group_size,
        erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit,
        reader, reader_bindings, c_element_count / c_group_size
    );

    std::vector<uint32_t> expected(c_element_count);
    for (uint32_t i = 0; i < c_element_count; ++i) {
        const uint32_t j      = c_element_count - 1u - i;
        const float    weight = static_cast<float>(j) * 0.5f;
        expected[i] = j + (static_cast<uint32_t>(weight * 2.0f) * 3u) + ((1u << (j % 32u)) ^ (0xABC00000u | i));
    }
    expect_output(*output, expected, "buffer_view_structured");
}

// agfx BufferViewConstant: a uniform block with a vec4 after three uints. The
// writer dispatch fills the buffer through a uint[] storage view at the word
// offsets Shader_resource reports for the std140 block; after a uniform
// memory_barrier the reader dispatch reads the same buffer as the uniform
// block and folds its members into an output SSBO; buffer golden
// buffer_view_constant.bin.
TEST_F(Buffer_binding_test, constant)
{
    erhe::graphics::Shader_resource params_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Params",
            .binding_point = 1,
            .type          = erhe::graphics::Shader_resource::Type::uniform_block
        }
    };
    const std::size_t off_a = params_block.add_uint("a")->get_offset_in_parent();
    const std::size_t off_b = params_block.add_uint("b")->get_offset_in_parent();
    const std::size_t off_c = params_block.add_uint("c")->get_offset_in_parent();
    const std::size_t off_v = params_block.add_vec4("v")->get_offset_in_parent();
    const std::size_t params_bytes = params_block.get_size_bytes();
    ASSERT_EQ(off_v % 16u, 0u) << "std140 aligns a vec4 to 16 bytes";

    const std::unique_ptr<erhe::graphics::Shader_resource> raw_block    = make_ssbo_block(device(), "Raw", 0);
    const std::unique_ptr<erhe::graphics::Shader_resource> output_block = make_ssbo_block(device(), "Output", 2);
    raw_block   ->add_uint("data", erhe::graphics::Shader_resource::unsized_array);
    output_block->add_uint("data", erhe::graphics::Shader_resource::unsized_array);

    const std::array<erhe::graphics::Binding_type, 3> types{
        erhe::graphics::Binding_type::storage_buffer,
        erhe::graphics::Binding_type::uniform_buffer,
        erhe::graphics::Binding_type::storage_buffer
    };
    const std::unique_ptr<erhe::graphics::Bind_group_layout> layout = make_layout(types, "constant layout");
    const std::vector<std::pair<std::string, std::string>> defines{
        { "GROUP_SIZE", std::to_string(c_group_size) + "u"                  },
        { "WORD_A",     std::to_string(off_a / sizeof(uint32_t)) + "u"      },
        { "WORD_B",     std::to_string(off_b / sizeof(uint32_t)) + "u"      },
        { "WORD_C",     std::to_string(off_c / sizeof(uint32_t)) + "u"      },
        { "WORD_V",     std::to_string(off_v / sizeof(uint32_t)) + "u"      }
    };
    const Compute_program writer = make_compute_program("constant_writer", c_constant_writer_source, defines, {}, { raw_block.get() }, *layout);
    const Compute_program reader = make_compute_program("constant_reader", c_constant_reader_source, defines, {}, { &params_block, output_block.get() }, *layout);
    ASSERT_TRUE(writer.is_valid());
    ASSERT_TRUE(reader.is_valid());

    const std::size_t output_bytes = static_cast<std::size_t>(c_constant_out) * sizeof(uint32_t);
    const std::shared_ptr<erhe::graphics::Buffer> params = make_host_buffer(
        params_bytes,
        erhe::graphics::Buffer_usage::uniform | erhe::graphics::Buffer_usage::storage,
        "constant params"
    );
    const std::shared_ptr<erhe::graphics::Buffer> output = make_readback_buffer(output_bytes, "constant output");
    const std::array<Dispatch_binding, 1> writer_bindings{
        Dispatch_binding{ erhe::graphics::Buffer_target::storage, params.get(), params_bytes, 0 }
    };
    const std::array<Dispatch_binding, 2> reader_bindings{
        Dispatch_binding{ erhe::graphics::Buffer_target::uniform, params.get(), params_bytes, 1 },
        Dispatch_binding{ erhe::graphics::Buffer_target::storage, output.get(), output_bytes, 2 }
    };
    write_then_read(
        *layout,
        writer, writer_bindings, 1,
        erhe::graphics::Memory_barrier_mask::uniform_barrier_bit,
        reader, reader_bindings, c_constant_out / c_group_size
    );

    const std::array<float, 4> v{ 1.5f, 2.25f, 3.125f, 4.0f };
    std::vector<uint32_t> expected(c_constant_out);
    for (uint32_t i = 0; i < c_constant_out; ++i) {
        expected[i] = (1000u * i) + (0x5A5Au ^ i) + 77u + static_cast<uint32_t>(v[i % 4u] * 8.0f);
    }
    expect_output(*output, expected, "buffer_view_constant");
}

} // namespace erhe::graphics::test
