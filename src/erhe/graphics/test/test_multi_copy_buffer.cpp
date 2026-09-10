// Multi_copy_buffer (doc/draw_list_material_set_plan.md D9): storage for a
// payload written only when its owner has something new to say, re-bound
// unchanged on every frame in between.
//
// These need a Device - the copy choice is driven by the backend's
// frame-completion predicate and by bind() stamping the current frame - so
// they live in the device-backed target rather than the deviceless one. They
// run against whatever backend the build was configured with, the null one
// included.

#include "gpu_test_fixture.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/multi_copy_buffer.hpp"
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

constexpr std::size_t c_payload_bytes = 16;  // std140 rounds a single-uint block up to 16

// Copies the value out of the bound uniform block into the storage block, so a
// readback shows which copy of the multi-copy buffer was actually bound.
constexpr const char* c_compute_source = R"glsl(
layout(local_size_x = 1) in;
void main()
{
    Output.data[0] = Params.value;
}
)glsl";

[[nodiscard]] auto make_create_info(const std::size_t copy_count) -> erhe::graphics::Multi_copy_buffer_create_info
{
    return erhe::graphics::Multi_copy_buffer_create_info{
        .initial_copy_byte_count = c_payload_bytes,
        .copy_count              = copy_count,
        .buffer_target           = erhe::graphics::Buffer_target::uniform,
        .binding_point           = 0u,
        .debug_label             = erhe::utility::Debug_label{"multi copy test"}
    };
}

void write_value(erhe::graphics::Multi_copy_buffer& buffer, const uint32_t value)
{
    const std::span<std::byte> span = buffer.begin_write(c_payload_bytes);
    ASSERT_GE(span.size(), sizeof(uint32_t));
    std::memset(span.data(), 0, c_payload_bytes);
    std::memcpy(span.data(), &value, sizeof(value));
    buffer.commit(c_payload_bytes);
}

} // anonymous namespace

// Everything the compute readback needs: a pipeline whose uniform block is the
// multi-copy buffer and whose storage block is the readback target.
class Multi_copy_test : public Gpu_test
{
protected:
    void build_pipeline()
    {
        m_ubo_block = std::make_unique<erhe::graphics::Shader_resource>(
            device(),
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "Params",
                .binding_point = 0,
                .type          = erhe::graphics::Shader_resource::Type::uniform_block
            }
        );
        m_ubo_block->add_uint("value");

        m_ssbo_block = std::make_unique<erhe::graphics::Shader_resource>(
            device(),
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "Output",
                .binding_point = 1,
                .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
                .writeonly     = true
            }
        );
        m_ssbo_block->add_uint("data", erhe::graphics::Shader_resource::unsized_array);

        m_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
            device(),
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 0u,
                        .type          = erhe::graphics::Binding_type::uniform_buffer,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                    },
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 1u,
                        .type          = erhe::graphics::Binding_type::storage_buffer,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                    }
                },
                .debug_label       = erhe::utility::Debug_label{"multi copy layout"},
                .uses_texture_heap = false
            }
        );

        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name              = "multi_copy",
            .interface_blocks  = { m_ubo_block.get(), m_ssbo_block.get() },
            .shaders           = { { erhe::graphics::Shader_type::compute_shader, std::string_view{c_compute_source} } },
            .bind_group_layout = m_layout.get()
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
        ASSERT_TRUE(prototype.is_valid()) << "multi-copy compute shader failed to compile/link";
        m_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(device(), std::move(prototype));

        m_pipeline = std::make_unique<erhe::graphics::Compute_pipeline>(
            device(),
            erhe::graphics::Compute_pipeline_data{
                .name              = "multi_copy",
                .shader_stages     = m_shader_stages.get(),
                .bind_group_layout = m_layout.get()
            }
        );
        ASSERT_TRUE(m_pipeline->is_valid());

        m_ssbo = make_host_buffer(sizeof(uint32_t), erhe::graphics::Buffer_usage::storage, "multi copy readback");
    }

    // One frame: bind the buffer's current copy and dispatch the copy-out.
    // record_extra runs inside the same frame, after the bind, so a test can
    // observe what begin_write() picks while this frame is still open.
    void bind_and_dispatch(
        erhe::graphics::Multi_copy_buffer&  buffer,
        const std::function<void()>&        record_extra = {}
    )
    {
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(m_layout.get());
                encoder.set_compute_pipeline(*m_pipeline);
                const bool bound = buffer.bind(encoder);
                EXPECT_TRUE(bound);
                encoder.set_buffer(erhe::graphics::Buffer_target::storage, m_ssbo.get(), 0, sizeof(uint32_t), 1);
                encoder.dispatch_compute(1, 1, 1);
                if (record_extra) {
                    record_extra();
                }
            }
        );
    }

    [[nodiscard]] auto read_back() -> uint32_t
    {
        const std::vector<std::byte> raw = read_buffer(*m_ssbo, sizeof(uint32_t));
        uint32_t value = 0;
        std::memcpy(&value, raw.data(), sizeof(value));
        return value;
    }

    std::unique_ptr<erhe::graphics::Shader_resource>   m_ubo_block;
    std::unique_ptr<erhe::graphics::Shader_resource>   m_ssbo_block;
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_layout;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_pipeline;
    std::shared_ptr<erhe::graphics::Buffer>            m_ssbo;
};

TEST_F(Multi_copy_test, first_commit_becomes_current)
{
    erhe::graphics::Multi_copy_buffer buffer{device(), make_create_info(3)};
    EXPECT_FALSE(buffer.has_current());
    EXPECT_EQ(buffer.get_commit_count(), 0u);

    write_value(buffer, 42u);
    EXPECT_TRUE(buffer.has_current());
    EXPECT_EQ(buffer.get_current_byte_count(), c_payload_bytes);
    EXPECT_EQ(buffer.get_commit_count(), 1u);
}

// Nothing committed: there is no copy to bind, and bind() must say so rather
// than bind whatever memory happens to be at offset zero.
TEST_F(Multi_copy_test, bind_before_commit_returns_false)
{
    build_pipeline();
    erhe::graphics::Multi_copy_buffer buffer{device(), make_create_info(3)};
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(m_layout.get());
            encoder.set_compute_pipeline(*m_pipeline);
            EXPECT_FALSE(buffer.bind(encoder));
        }
    );
}

// The point of the whole construct: frames that change nothing re-bind the copy
// that is already there, writing no new one.
TEST_F(Multi_copy_test, clean_frames_rebind_same_offset)
{
    build_pipeline();
    erhe::graphics::Multi_copy_buffer buffer{device(), make_create_info(3)};
    write_value(buffer, 7u);
    const std::size_t offset = buffer.get_current_byte_offset();

    for (int i = 0; i < 8; ++i) {
        bind_and_dispatch(buffer);
        EXPECT_EQ(buffer.get_current_byte_offset(), offset);
        EXPECT_EQ(read_back(), 7u);
    }
    EXPECT_EQ(buffer.get_commit_count(), 1u) << "a frame with no new content wrote a copy";
    EXPECT_EQ(buffer.get_allocation_count(), 1u);
}

TEST_F(Multi_copy_test, begin_write_never_returns_the_current_copy)
{
    erhe::graphics::Multi_copy_buffer buffer{device(), make_create_info(3)};
    write_value(buffer, 1u);
    const std::size_t first = buffer.get_current_byte_offset();
    write_value(buffer, 2u);
    const std::size_t second = buffer.get_current_byte_offset();
    EXPECT_NE(first, second) << "a write landed in the copy that was current";
}

// The stamping rule: a copy is in use by every frame that BOUND it, not only
// by the frame that wrote it. Bind two copies inside one still-open frame and
// a third write must go somewhere else again - against a commit-time stamp the
// first copy would look free and be overwritten while this frame reads it.
TEST_F(Multi_copy_test, begin_write_refuses_a_copy_this_frame_bound)
{
    build_pipeline();
    erhe::graphics::Multi_copy_buffer buffer{device(), make_create_info(3)};
    write_value(buffer, 10u);
    const std::size_t first = buffer.get_current_byte_offset();

    std::size_t second = 0;
    std::size_t third  = 0;
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(m_layout.get());
            encoder.set_compute_pipeline(*m_pipeline);
            EXPECT_TRUE(buffer.bind(encoder));          // this frame reads `first`
            write_value(buffer, 11u);
            second = buffer.get_current_byte_offset();
            EXPECT_TRUE(buffer.bind(encoder));          // and now also `second`
            write_value(buffer, 12u);
            third = buffer.get_current_byte_offset();
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, m_ssbo.get(), 0, sizeof(uint32_t), 1);
            encoder.dispatch_compute(1, 1, 1);
        }
    );

    EXPECT_NE(second, first);
    EXPECT_NE(third,  first) << "a copy this frame bound was handed out for writing";
    EXPECT_NE(third,  second) << "a copy this frame bound was handed out for writing";
}

// With every copy in use by the open frame, the buffer must not stall and must
// not overwrite: it takes a fresh allocation and retires the old one.
TEST_F(Multi_copy_test, exhausting_the_copies_allocates_rather_than_stalling)
{
    build_pipeline();
    erhe::graphics::Multi_copy_buffer buffer{device(), make_create_info(2)};
    write_value(buffer, 20u);
    ASSERT_EQ(buffer.get_allocation_count(), 1u);

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(m_layout.get());
            encoder.set_compute_pipeline(*m_pipeline);
            EXPECT_TRUE(buffer.bind(encoder));
            write_value(buffer, 21u);   // takes the only other copy
            EXPECT_TRUE(buffer.bind(encoder));
            write_value(buffer, 22u);   // both are in use by this frame -> reallocate
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, m_ssbo.get(), 0, sizeof(uint32_t), 1);
            encoder.dispatch_compute(1, 1, 1);
        }
    );
    EXPECT_EQ(buffer.get_allocation_count(), 2u) << "the buffer neither grew nor stalled";
}

TEST_F(Multi_copy_test, write_larger_than_the_current_copy_grows_the_buffer)
{
    build_pipeline();
    erhe::graphics::Multi_copy_buffer buffer{device(), make_create_info(3)};
    write_value(buffer, 30u);
    ASSERT_EQ(buffer.get_allocation_count(), 1u);
    const std::size_t small = buffer.get_copy_byte_count();

    const std::size_t large_bytes = small * 4;
    {
        const std::span<std::byte> span = buffer.begin_write(large_bytes);
        ASSERT_GE(span.size(), large_bytes);
        std::memset(span.data(), 0, large_bytes);
        const uint32_t value = 31u;
        std::memcpy(span.data(), &value, sizeof(value));
        buffer.commit(large_bytes);
    }
    EXPECT_GE(buffer.get_copy_byte_count(), large_bytes);
    EXPECT_EQ(buffer.get_allocation_count(), 2u);

    // The whole new payload is there and binds: growth reallocates rather than
    // copying forward, so the write that triggered it must be complete.
    bind_and_dispatch(buffer);
    EXPECT_EQ(read_back(), 31u);
}

// A superseded allocation must outlive the frames that bound it.
TEST_F(Multi_copy_test, the_superseded_allocation_outlives_the_frames_that_bound_it)
{
    build_pipeline();
    erhe::graphics::Multi_copy_buffer buffer{device(), make_create_info(3)};
    write_value(buffer, 40u);
    bind_and_dispatch(buffer);          // a frame now has this allocation bound

    const std::span<std::byte> span = buffer.begin_write(buffer.get_copy_byte_count() * 4);
    std::memset(span.data(), 0, span.size());
    const uint32_t value = 41u;
    std::memcpy(span.data(), &value, sizeof(value));
    buffer.commit(buffer.get_copy_byte_count());
    EXPECT_EQ(buffer.get_allocation_count(), 2u);

    // submit_and_wait() retires the frame it drove, so the retired allocation
    // is released on the next call that sweeps.
    bind_and_dispatch(buffer);
    EXPECT_EQ(read_back(), 41u);
    static_cast<void>(buffer.begin_write(sizeof(uint32_t)));
    EXPECT_EQ(buffer.get_retired_count(), 0u) << "a retired allocation was never released";
}

// The end-to-end statement: write and render on every frame with different
// content each time and read each frame's result back; every frame must show
// its own content, never a neighbour's.
TEST_F(Multi_copy_test, copies_recycle_when_updating_every_frame)
{
    build_pipeline();
    erhe::graphics::Multi_copy_buffer buffer{device(), make_create_info(0)};  // device sizing
    const std::size_t frames = 3 * buffer.get_copy_count();
    for (std::size_t i = 0; i < frames; ++i) {
        const uint32_t value = static_cast<uint32_t>(100 + i);
        write_value(buffer, value);
        bind_and_dispatch(buffer);
        EXPECT_EQ(read_back(), value) << "frame " << i << " did not see its own content";
    }
    EXPECT_EQ(buffer.get_commit_count(), frames);
}

} // namespace erhe::graphics::test
